/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![cfg(feature = "stateful")]

// Testing the two phase updates.
// This test crashes lmdb for reasons that make no sense, so only run it
// in the "safe mode" backend.

mod common;

use nimbus::NimbusClient;
use nimbus::error::{Result, debug};

use crate::common::{
    create_database, exactly_two_experiments, new_test_client, new_test_client_with_db,
    no_test_experiments, two_valid_experiments,
};

fn startup(client: &NimbusClient, first_run: bool) -> Result<()> {
    if first_run {
        client.set_experiments_locally(exactly_two_experiments())?;
    }
    client.apply_pending_experiments()?;
    client.fetch_experiments()?;
    Ok(())
}

#[test]
fn test_two_phase_update() -> Result<()> {
    let client = new_test_client("test_two_phase_update")?;
    client.fetch_experiments()?;

    // We have fetched the experiments from the server, but not put them into use yet.
    // The experiments are pending.
    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), 0);

    // Now, the app chooses when to apply the pending updates to the experiments.
    let events: Vec<_> = client.apply_pending_experiments()?;
    assert_eq!(events.len(), 1);

    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), 1);
    assert_eq!(experiments[0].slug, "secure-gold");

    // Next time we start the app, we immediately apply pending updates,
    // but there may not be any waiting.
    let events: Vec<_> = client.apply_pending_experiments()?;
    // No change events
    assert_eq!(events.len(), 0);

    // Confirm that nothing has changed.
    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), 1);
    assert_eq!(experiments[0].slug, "secure-gold");

    Ok(())
}

fn assert_experiment_count(client: &NimbusClient, count: usize) -> Result<()> {
    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), count);
    Ok(())
}

#[test]
fn test_set_experiments_locally() -> Result<()> {
    let client = new_test_client("test_set_experiments_locally")?;
    assert_experiment_count(&client, 0)?;

    client.set_experiments_locally(exactly_two_experiments())?;
    assert_experiment_count(&client, 0)?;

    client.apply_pending_experiments()?;
    assert_experiment_count(&client, 2)?;

    client.set_experiments_locally(no_test_experiments())?;
    assert_experiment_count(&client, 2)?;

    client.apply_pending_experiments()?;
    assert_experiment_count(&client, 0)?;

    Ok(())
}

#[test]
fn test_startup_behavior() -> Result<()> {
    let client = new_test_client("test_startup_behavior")?;
    startup(&client, true)?;

    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), 2);
    assert_eq!(experiments[0].slug, "secure-gold");
    assert_eq!(experiments[1].slug, "startup-gold");

    // The app is at a safe place to change all the experiments.
    client.apply_pending_experiments()?;
    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), 1);
    assert_eq!(experiments[0].slug, "secure-gold");

    // Next time we start the app.
    startup(&client, false)?;
    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), 1);
    assert_eq!(experiments[0].slug, "secure-gold");

    Ok(())
}

use serde_json::json;

// forked from persistence.rs; need to be merged in a separated test-utils
// module usable both by unit and integration tests (nimbus/test-utils?)
pub fn get_db_v1_experiments_with_missing_feature_fields() -> Vec<serde_json::Value> {
    vec![
        json!({
            "schemaVersion": "1.0.0",
            "slug": "branch-feature-empty-obj", // change when copy/pasting to make experiments
            "endDate": null,
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {}
                },
                {
                    "slug": "treatment",
                    "ratio":1,
                    "feature": {}
                }
            ],
            "channel": "nightly",
            "probeSets":[],
            "startDate":null,
            "appName": "fenix",
            "appId": "org.mozilla.fenix",
            "bucketConfig":{
                // Setup to enroll everyone by default.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"branch-feature-empty-obj", // change when copy/pasting to make experiments
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"Diagnostic test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        }),
        json!({
            "schemaVersion": "1.0.0",
            "slug": "missing-branch-feature-clause", // change when copy/pasting to make experiments
            "endDate": null,
            "featureIds": ["aaa"], // change when copy/pasting to make experiments
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                },
                {
                    "slug": "treatment",
                    "ratio":1,
                    "feature": {
                        "featureId": "aaa", // change when copy/pasting to make experiments
                        "enabled": true,
                        "value": {},
                    }
                }
            ],
            "channel": "nightly",
            "probeSets":[],
            "startDate":null,
            "appName": "fenix",
            "appId": "org.mozilla.fenix",
            "bucketConfig":{
                // Setup to enroll everyone by default.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"empty-branch-feature-clause", // change when copy/pasting to make experiments
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"Diagnostic test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        }),
        json!({
            "schemaVersion": "1.0.0",
            "slug": "branch-feature-feature-id-missing", // change when copy/pasting to make experiments
            "endDate": null,
            "featureIds": ["ccc"], // change when copy/pasting to make experiments
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {
                        "featureId": "ccc", // change when copy/pasting to make experiments
                        "enabled": false,
                        "value": {}
                    }
                },
                {
                    "slug": "treatment",
                    "ratio":1,
                    "feature": {
                        "enabled": true,
                        "value": {}
                    }
                }
            ],
            "channel": "nightly",
            "probeSets":[],
            "startDate":null,
            "appName": "fenix",
            "appId": "org.mozilla.fenix",
            "bucketConfig":{
                // Setup to enroll everyone by default.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"branch-feature-feature-id-missing", // change when copy/pasting to make experiments
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"Diagnostic test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        }),
        json!({
            "schemaVersion": "1.0.0",
            "slug": "feature-ids-array-has-empty_string", // change when copy/pasting to make experiments
            "endDate": null,
            "featureIds": [""], // change when copy/pasting to make experiments
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {
                        "featureId": "def", // change when copy/pasting to make experiments
                        "enabled": false,
                        "value": {},
                    }
                },
                {
                    "slug": "treatment",
                    "ratio":1,
                    "feature": {
                        "featureId": "def", // change when copy/pasting to make experiments
                        "enabled": true,
                        "value": {}
                    }
                }
            ],
            "channel": "nightly",
            "probeSets":[],
            "startDate":null,
            "appName": "fenix",
            "appId": "org.mozilla.fenix",
            "bucketConfig":{
                // Setup to enroll everyone by default.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"feature-ids-array-has-empty-string", // change when copy/pasting to make experiments
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"Diagnostic test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        }),
        json!({
            "schemaVersion": "1.0.0",
            "slug": "missing-feature-ids-in-branch",
            "endDate": null,
            "featureIds": ["abc"],
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {
                        "enabled": true,
                        "value": {}
                    }
                },
                {
                    "slug": "treatment",
                    "ratio": 1,
                    "feature": {
                        "enabled": true,
                        "value": {}
                    }
                }
            ],
            "probeSets":[],
            "startDate":null,
            "appName":"fenix",
            "appId":"org.mozilla.fenix",
            "channel":"nightly",
            "bucketConfig":{
                // Setup to enroll everyone by default.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"no-feature-ids-at-all",
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"Diagnostic test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        }),
        json!({
            "schemaVersion": "1.0.0",
            "slug": "missing-featureids-array", // change when copy/pasting to make experiments
            "endDate": null,
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {
                        "featureId": "about_welcome", // change when copy/pasting to make experiments
                        "enabled": false,
                        "value": {}
                    }
                },
                {
                    "slug": "treatment",
                    "ratio":1,
                    "feature": {
                        "featureId": "about_welcome", // change when copy/pasting to make experiments
                        "enabled": true,
                        "value": {}
                    }
                }
            ],
            "channel": "nightly",
            "probeSets":[],
            "startDate":null,
            "appName": "fenix",
            "appId": "org.mozilla.fenix",
            "bucketConfig":{
                // Setup to enroll everyone by default.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"valid-feature-experiment", // change when copy/pasting to make experiments
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"Diagnostic test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        }),
        json!({
            "schemaVersion": "1.0.0",
            "slug": "branch-feature-feature-id-empty", // change when copy/pasting to make experiments
            "endDate": null,
            "featureIds": [""], // change when copy/pasting to make experiments
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {
                        "featureId": "", // change when copy/pasting to make experiments
                        "enabled": false,
                        "value": {},
                    }
                },
                {
                    "slug": "treatment",
                    "ratio":1,
                    "feature": {
                        "featureId": "", // change when copy/pasting to make experiments
                        "enabled": true,
                        "value": {},
                    }
                }
            ],
            "channel": "nightly",
            "probeSets":[],
            "startDate":null,
            "appName": "fenix",
            "appId": "org.mozilla.fenix",
            "bucketConfig":{
                // Setup to enroll everyone by default.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"branch-feature-feature-id-empty", // change when copy/pasting to make experiments
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"Diagnostic test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        }),
    ]
}

/// test that even with a database with orphan records,
/// apply_pending_experiments does not throw an error
#[test]
fn test_startup_orphan_behavior() -> Result<()> {
    error_support::init_for_tests();

    // these enrollments should cause orphan experiments to be created
    let enrollments_for_missing_feature_id = vec![
        json!(
        {
            "slug": "missing-feature-ids-in-branch",
            "status":
                {
                    "Enrolled":
                        {
                            "enrollment_id": "801ee64b-0b1b-44a7-be47-5f1b5c189084",
                            "reason": "Qualified",
                            "branch": "control",
                            "feature_id": ""
                        }
                }
            }
        ),
        json!(
        {
            "slug": "branch-feature-empty-obj",
            "status":
                {
                    "Enrolled":
                        {
                            "enrollment_id": "801ee64b-0b1b-44a7-be47-5f1b5c18984",
                            "reason": "Qualified",
                            "branch": "control",
                            //"feature_id": ""
                        }
                }
            }
        ),
    ];

    let tmp_dir = tempfile::tempdir()?;
    let db_v1_experiments_with_missing_feature_fields =
        &get_db_v1_experiments_with_missing_feature_fields();

    // create a database in a way that could cause the migrator to
    // leave some orphan records while cleaning up...
    create_database(
        &tmp_dir,
        1,
        db_v1_experiments_with_missing_feature_fields,
        &enrollments_for_missing_feature_id,
    )?;

    let client = new_test_client_with_db(&tmp_dir)?;

    let experiments = client.get_all_experiments()?;
    debug!("after db creation: experiments = {:?}", experiments);

    assert_eq!(experiments.len(), 0); // all data should have been discarded

    // Apply the new experiments from the test dir...
    client.fetch_experiments()?;
    client.apply_pending_experiments()?;

    let experiments = client.get_all_experiments()?;
    debug!(
        "after 2nd apply and get_all: experiments = {:?}",
        experiments
    );

    // Make sure that we have what we should...
    assert_eq!(experiments.len(), 1);
    assert_eq!(experiments[0].slug, "secure-gold");

    Ok(())
}

/// Test that with pre-existing experiments that are missing enrollments,
/// apply_pending_experiments drops the experiments and returns the right
/// thing.
#[test]
fn test_experiments_without_enrollments_are_dropped() -> Result<()> {
    error_support::init_for_tests();

    let tmp_dir = tempfile::tempdir()?;
    let two_valid_experiments = &two_valid_experiments();

    // create a database in a way that could cause the migrator to
    // leave some orphan records while cleaning up by having pre-existing
    // enrollments for the pre-existing experiments, which should cause an
    // error during evolution forcing both records to be dropped.
    create_database(&tmp_dir, 2, two_valid_experiments, &[])?;

    let client = new_test_client_with_db(&tmp_dir)?;

    let experiments = client.get_all_experiments()?;
    debug!("after db creation: experiments = {:?}", experiments);

    assert_eq!(
        experiments.len(),
        2,
        "both experiments should have been read"
    );

    // Apply the new experiments from the test dir...
    client.fetch_experiments()?;
    client.apply_pending_experiments()?;

    let experiments = client.get_all_experiments()?;
    debug!(
        "after 2nd apply and get_all: experiments = {:?}",
        experiments
    );

    // Make sure that we have what we should...
    assert_eq!(
        experiments.len(),
        0,
        "both experiments should have been discarded"
    );

    Ok(())
}
