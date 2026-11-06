/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![cfg(feature = "stateful")]

mod common;

use std::collections::HashSet;

use nimbus::{NimbusError, Result};
use serde_json::json;

use crate::common::new_test_client_with_db;

fn experiment_target_false() -> serde_json::Value {
    json!({
        "schemaVersion": "1.0.0",
        "slug": "experiment_target_false",
        "endDate": null,
        "featureIds": ["some-feature"],
        "branches": [
            {
            "slug": "control",
            "ratio": 1
            },
            {
            "slug": "treatment",
            "ratio": 1
            }
        ],
        "channel": "nightly",
        "probeSets": [],
        "startDate": null,
        "appName": "fenix",
        "appId": "org.mozilla.fenix",
        "bucketConfig": {
            "count": 10000,
            "start": 0,
            "total": 10000,
            "namespace": "experiment_target_false",
            "randomizationUnit": "nimbus_id"
        },
        "targeting": "false",
        "userFacingName": "Diagnostic test experiment",
        "referenceBranch": "control",
        "isEnrollmentPaused": false,
        "proposedEnrollment": 7,
        "userFacingDescription": "This is a test experiment for diagnostic purposes.",
        "id": "secure-copper",
        "last_modified": 1_602_197_324_372i64,
    })
}

fn experiment_zero_buckets() -> serde_json::Value {
    json!({
        "schemaVersion": "1.0.0",
        "slug": "experiment_zero_buckets",
        "endDate": null,
        "featureIds": ["some-feature"],
        "branches": [
            {
            "slug": "control",
            "ratio": 1
            },
            {
            "slug": "treatment",
            "ratio": 1
            }
        ],
        "channel": "nightly",
        "probeSets": [],
        "startDate": null,
        "appName": "fenix",
        "appId": "org.mozilla.fenix",
        "bucketConfig": {
            "count": 0,
            "start": 0,
            "total": 10000,
            "namespace": "experiment_zero_buckets",
            "randomizationUnit": "nimbus_id"
        },
        "userFacingName": "Diagnostic test experiment",
        "referenceBranch": "control",
        "isEnrollmentPaused": false,
        "proposedEnrollment": 7,
        "userFacingDescription": "This is a test experiment for diagnostic purposes.",
        "id": "secure-copper",
        "last_modified": 1_602_197_324_372i64,
    })
}

fn experiment_always_enroll() -> serde_json::Value {
    json!({
        "schemaVersion": "1.0.0",
        "slug": "experiment_always_enroll",
        "endDate": null,
        "featureIds": ["some-feature"],
        "branches": [
            {
            "slug": "treatment",
            "ratio": 1
            }
        ],
        "channel": "nightly",
        "probeSets": [],
        "startDate": null,
        "appName": "fenix",
        "appId": "org.mozilla.fenix",
        "bucketConfig": {
            "count": 10000,
            "start": 0,
            "total": 10000,
            "namespace": "experiment_always_enroll",
            "randomizationUnit": "nimbus_id"
        },
        "userFacingName": "Diagnostic test experiment",
        "referenceBranch": "control",
        "isEnrollmentPaused": false,
        "proposedEnrollment": 7,
        "userFacingDescription": "This is a test experiment for diagnostic purposes.",
        "id": "secure-copper",
        "last_modified": 1_602_197_324_372i64,
        "targeting": "true",
    })
}

#[test]
fn test_restart_opt_in() -> Result<()> {
    let temp_dir = tempfile::tempdir()?;
    let client = new_test_client_with_db(&temp_dir)?;
    client.initialize()?;
    let experiment_json = match serde_json::to_string(&json!({
        "data": [
            experiment_target_false(),
            experiment_zero_buckets(),
        ]
    })) {
        Ok(v) => v,
        Err(e) => return Err(NimbusError::JSONError("test".into(), e.to_string())),
    };
    client.set_experiments_locally(experiment_json.clone())?;
    client.apply_pending_experiments()?;
    // the experiment_target_false experiment has a 'targeting' of "false", we test to ensure that
    // restarting the app preserves the fact that we opt-ed in, even though we were not
    // targeted
    client.opt_in_with_branch("experiment_target_false".into(), "treatment".into())?;
    // the experiment_zero_buckets experiment has a bucket configuration of 0%, meaning we will always not
    // be enrolled, we test to ensure that is overridden when we opt-in
    client.opt_in_with_branch("experiment_zero_buckets".into(), "treatment".into())?;

    let before_restart_experiments = client.get_active_experiments()?;
    assert_eq!(before_restart_experiments.len(), 2);
    assert_eq!(before_restart_experiments[0].branch_slug, "treatment");
    assert_eq!(before_restart_experiments[1].branch_slug, "treatment");
    // we drop the NimbusClient to terminate the underlying database connection
    drop(client);

    let client = new_test_client_with_db(&temp_dir)?;
    client.initialize()?;
    client.set_experiments_locally(experiment_json)?;
    client.apply_pending_experiments()?;
    let after_restart_experiments = client.get_active_experiments()?;
    assert_eq!(
        before_restart_experiments.len(),
        after_restart_experiments.len()
    );
    assert_eq!(after_restart_experiments[0].branch_slug, "treatment");
    assert_eq!(after_restart_experiments[1].branch_slug, "treatment");

    Ok(())
}

#[test]
fn test_targeting_attributes_active_experiments() -> Result<()> {
    let temp_dir = tempfile::tempdir()?;
    let client = new_test_client_with_db(&temp_dir)?;

    // On construction, the active_experiments in targeting attributes is empty.
    let expected = HashSet::new();
    let ta = client.get_targeting_attributes();
    assert_eq!(ta.active_experiments, expected);

    let experiment_json = match serde_json::to_string(&json!({
        "data": [
            experiment_target_false(),
            experiment_zero_buckets(),
            experiment_always_enroll(),
        ]
    })) {
        Ok(v) => v,
        Err(e) => return Err(NimbusError::JSONError("test".into(), e.to_string())),
    };
    client.set_experiments_locally(experiment_json)?;
    client.apply_pending_experiments()?;

    let expected = ["experiment_always_enroll"]
        .iter()
        .map(|s| s.to_string())
        .collect::<HashSet<_>>();
    let ta = client.get_targeting_attributes();
    assert_eq!(ta.active_experiments, expected);

    // Opting in or out should keep the targeting attributes up to date.
    client.opt_in_with_branch("experiment_target_false".into(), "treatment".into())?;

    let expected = ["experiment_always_enroll", "experiment_target_false"]
        .iter()
        .map(|s| s.to_string())
        .collect::<HashSet<_>>();
    let ta = client.get_targeting_attributes();
    assert_eq!(ta.active_experiments, expected);

    let eval = client.create_targeting_helper(None)?;
    assert!(eval.eval_jexl("'experiment_always_enroll' in active_experiments")?);
    assert!(eval.eval_jexl("'experiment_target_false' in active_experiments")?);
    assert!(!eval.eval_jexl("'experiment_zero_buckets' in active_experiments")?);

    drop(client);

    // On restart, we might only do an initialize
    let client = new_test_client_with_db(&temp_dir)?;
    client.initialize()?;
    let ta = client.get_targeting_attributes();
    assert_eq!(ta.active_experiments, expected);

    let eval = client.create_targeting_helper(None)?;
    assert!(eval.eval_jexl("'experiment_always_enroll' in active_experiments")?);
    assert!(eval.eval_jexl("'experiment_target_false' in active_experiments")?);
    assert!(!eval.eval_jexl("'experiment_zero_buckets' in active_experiments")?);

    drop(client);

    // On another restart, we might do an apply_pending_experiments, with nothing pending.
    let client = new_test_client_with_db(&temp_dir)?;
    client.apply_pending_experiments()?;
    let ta = client.get_targeting_attributes();
    assert_eq!(ta.active_experiments, expected);

    let eval = client.create_targeting_helper(None)?;
    assert!(eval.eval_jexl("'experiment_always_enroll' in active_experiments")?);
    assert!(eval.eval_jexl("'experiment_target_false' in active_experiments")?);
    assert!(!eval.eval_jexl("'experiment_zero_buckets' in active_experiments")?);

    Ok(())
}
