/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![cfg(feature = "stateful")]

// utilities shared between tests

use nimbus::error::{Result, debug};
use nimbus::metrics::{
    DatabaseLoadExtraDef, DatabaseMigrationExtraDef, EnrollmentStatusExtraDef,
    FeatureExposureExtraDef, MalformedFeatureConfigExtraDef, MetricsHandler,
};
use nimbus::stateful::client::NimbusServerSettings;
use nimbus::stateful::persistence::{Database, SingleStore};
use nimbus::{AppContext, NimbusClient, RemoteSettingsServer};
use remote_settings::{RemoteSettingsConfig, RemoteSettingsContext, RemoteSettingsService};
use rkv::StoreOptions;
use std::{path::Path, sync::Arc};

pub struct NoopMetricsHandler;

#[cfg(feature = "stateful")]
impl MetricsHandler for NoopMetricsHandler {
    fn record_database_load(&self, _: DatabaseLoadExtraDef) {
        // do nothing
    }

    fn record_database_migration(&self, _: DatabaseMigrationExtraDef) {
        // do nothing
    }

    fn record_enrollment_statuses(&self, _: Vec<EnrollmentStatusExtraDef>) {
        // do nothing
    }

    fn record_feature_activation(&self, _activation_event: FeatureExposureExtraDef) {
        // do nothing
    }

    fn record_feature_exposure(&self, _activation_event: FeatureExposureExtraDef) {
        // do nothing
    }

    fn record_malformed_feature_config(&self, _event: MalformedFeatureConfigExtraDef) {
        // do nothing
    }

    fn submit_targeting_context(&self) {
        // do nothing
    }

    fn record_rollouts_opt_out(&self) {
        // do nothing
    }

    fn record_studies_opt_out(&self) {
        // do nothing
    }
}

#[cfg(not(feature = "stateful"))]
impl MetricsHandler for NoopMetricsHandler {
    fn record_enrollment_statuses_v2(&self, _: Vec<EnrollmentStatusExtraDef>, _: Option<String>) {
        // do nothing
    }
}

#[allow(dead_code)] // work around https://github.com/rust-lang/rust/issues/46379
pub fn new_test_client(_identifier: &str) -> Result<NimbusClient> {
    let tmp_dir = tempfile::tempdir()?;
    new_test_client_internal(&tmp_dir)
}

#[allow(dead_code)] // work around https://github.com/rust-lang/rust/issues/46379
pub fn new_test_client_with_db(tmp_dir: &tempfile::TempDir) -> Result<NimbusClient> {
    new_test_client_internal(tmp_dir)
}

fn new_test_client_internal(
    tmp_dir: &tempfile::TempDir,
) -> Result<NimbusClient, nimbus::NimbusError> {
    use std::path::PathBuf;
    use url::Url;
    error_support::init_for_tests();
    let mut dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    dir.push("tests/experiments");
    let url = Url::from_file_path(dir).expect("experiments dir should exist");

    let ctx = AppContext {
        app_name: "fenix".to_string(),
        app_id: "org.mozilla.fenix".to_string(),
        channel: "nightly".to_string(),
        locale: Some("en-GB".to_string()),
        ..Default::default()
    };

    let rs_ctx = RemoteSettingsContext {
        channel: Some("nightly".to_string()),
        locale: Some("en-GB".to_string()),
        ..Default::default()
    };

    let config = RemoteSettingsConfig {
        server: Some(RemoteSettingsServer::Custom {
            url: url.as_str().to_string(),
        }),
        bucket_name: None,
        app_context: Some(rs_ctx),
    };
    let storage_dir = tmp_dir
        .path()
        .join("remote-settings")
        .to_string_lossy()
        .to_string();
    let remote_settings_service = RemoteSettingsService::new(storage_dir, config);

    NimbusClient::new(
        ctx,
        Default::default(),
        Default::default(),
        tmp_dir.path(),
        Arc::new(NoopMetricsHandler),
        None,
        Some(NimbusServerSettings {
            rs_service: Arc::new(remote_settings_service),
            collection_name: "collection_name".to_string(),
        }),
    )
}

#[allow(dead_code)] //  work around https://github.com/rust-lang/rust/issues/46379
pub fn create_database<P: AsRef<Path>>(
    path: P,
    old_version: u16,
    experiments_json: &[serde_json::Value],
    enrollments_json: &[serde_json::Value],
) -> Result<()> {
    error_support::init_for_tests();
    debug!("create_database(): old_version = {:?}", old_version);
    debug!("create_database(): path = {:?}", path.as_ref());
    let rkv = Database::open_rkv(path)?.0;
    let meta_store = SingleStore::new(rkv.open_single("meta", StoreOptions::create())?);
    let experiment_store =
        SingleStore::new(rkv.open_single("experiments", StoreOptions::create())?);
    let enrollment_store =
        SingleStore::new(rkv.open_single("enrollments", StoreOptions::create())?);
    let mut writer = rkv.write()?;

    meta_store.put(&mut writer, "db_version", &old_version)?;

    // write out the experiments
    for experiment_json in experiments_json {
        debug!("create_database(): experiment_json = {:?}", experiment_json);
        experiment_store.put(
            &mut writer,
            experiment_json["slug"].as_str().unwrap(),
            experiment_json,
        )?;
    }

    // write out the enrollments
    for enrollment_json in enrollments_json {
        // debug!("enrollment_json = {:?}", enrollment_json);
        enrollment_store.put(
            &mut writer,
            enrollment_json["slug"].as_str().unwrap(),
            enrollment_json,
        )?;
    }

    writer.commit()?;
    debug!("create_database: writer committed");

    Ok(())
}

#[allow(dead_code)] //  work around https://github.com/rust-lang/rust/issues/46379
pub fn exactly_two_experiments() -> String {
    use serde_json::json;
    json!({
        "data": [
            {
                "schemaVersion": "1.0.0",
                "slug": "startup-gold",
                "endDate": null,
                "featureIds": ["aboutmonkeys"],
                "branches":[
                    {
                        "slug": "control",
                        "ratio": 1,
                        "feature": {
                            "featureId": "aboutmonkeys",
                            "enabled": false
                        }
                    },
                    {
                        "slug": "treatment",
                        "ratio":1,
                        "feature": {
                            "featureId": "aboutmonkeys",
                            "enabled": true
                        },

                    }
                ],
                "channel": "nightly",
                "probeSets":[],
                "startDate":null,
                "appName":"fenix",
                "appId":"org.mozilla.fenix",
                "bucketConfig":{
                    // Setup to enroll everyone by default.
                    "count":10_000,
                    "start":0,
                    "total":10_000,
                    "namespace":"startup-gold",
                    "randomizationUnit":"nimbus_id"
                },
                "userFacingName":"Diagnostic test experiment",
                "referenceBranch":"control",
                "isEnrollmentPaused":false,
                "proposedEnrollment":7,
                "userFacingDescription":"This is a test experiment for diagnostic purposes.",
                "id":"startup-gold",
                "last_modified":1_602_197_324_372i64
            },
            {
                "schemaVersion": "1.0.0",
                "slug": "secure-gold",
                "endDate": null,
                "featureIds": ["aboutwelcome"],
                "branches":[
                    {
                        "slug": "control",
                        "ratio": 1,
                        "feature": {
                            "featureId": "aboutwelcome",
                            "enabled": false
                        },
                    },
                    {
                        "slug": "treatment",
                        "ratio":1,
                        "feature": {
                            "featureId": "aboutwelcome",
                            "enabled": true
                        },
                    }
                ],
                "channel": "nightly",
                "probeSets":[],
                "startDate":null,
                "appName":"fenix",
                "appId":"org.mozilla.fenix",
                "bucketConfig":{
                    // Setup to enroll everyone by default.
                    "count":10_000,
                    "start":0,
                    "total":10_000,
                    "namespace":"secure-gold",
                    "randomizationUnit":"nimbus_id"
                },
                "userFacingName":"Diagnostic test experiment",
                "referenceBranch":"control",
                "isEnrollmentPaused":false,
                "proposedEnrollment":7,
                "userFacingDescription":"This is a test experiment for diagnostic purposes.",
                "id":"secure-gold",
                "last_modified":1_602_197_324_372i64
            }
        ]
    })
    .to_string()
}

#[allow(dead_code)] //  work around https://github.com/rust-lang/rust/issues/46379
pub fn two_valid_experiments() -> Vec<serde_json::Value> {
    use serde_json::json;
    vec![
        json!({
        "schemaVersion": "1.0.0",
        "slug": "startup-gold",
        "endDate": null,
        "featureIds": ["aboutmonkeys"],
        "branches":[
            {
                "slug": "control",
                "ratio": 1,
                "feature": {
                    "featureId": "aboutmonkeys",
                    "enabled": false
                }
            },
            {
                "slug": "treatment",
                "ratio":1,
                "feature": {
                    "featureId": "aboutmonkeys",
                    "enabled": true
                },
            }
        ],
        "channel": "nightly",
        "probeSets":[],
        "startDate":null,
        "appName":"fenix",
        "appId":"org.mozilla.fenix",
        "bucketConfig":{
            // Setup to enroll everyone by default.
            "count":10_000,
            "start":0,
            "total":10_000,
            "namespace":"startup-gold",
            "randomizationUnit":"nimbus_id"
        },
        "userFacingName":"Diagnostic test experiment",
        "referenceBranch":"control",
        "isEnrollmentPaused":false,
        "proposedEnrollment":7,
        "userFacingDescription":"This is a test experiment for diagnostic purposes.",
        "id":"startup-gold",
        "last_modified":1_602_197_324_372i64
        }),
        json!({
                "schemaVersion": "1.0.0",
                "slug": "secure-gold",
                "endDate": null,
                "featureIds": ["some-feature"],
                "branches":[
                    {
                        "slug": "control",
                        "ratio": 1,
                        "feature": {
                            "featureId": "some-feature",
                            "enabled": false
                        },
                    },
                    {
                        "slug": "treatment",
                        "ratio":1,
                        "feature": {
                            "featureId": "some-feature",
                            "enabled": true
                        },
                    }
                ],
                "channel": "nightly",
                "probeSets":[],
                "startDate":null,
                "appName":"fenix",
                "appId":"org.mozilla.fenix",
                "bucketConfig":{
                    // Setup to enroll everyone by default.
                    "count":10_000,
                    "start":0,
                    "total":10_000,
                    "namespace":"secure-gold",
                    "randomizationUnit":"nimbus_id"
                },
                "userFacingName":"Diagnostic test experiment",
                "referenceBranch":"control",
                "isEnrollmentPaused":false,
                "proposedEnrollment":7,
                "userFacingDescription":"This is a test experiment for diagnostic purposes.",
                "id":"secure-gold",
                "last_modified":1_602_197_324_372i64
            }
        ),
    ]
}

#[allow(dead_code)] //  work around https://github.com/rust-lang/rust/issues/46379
pub fn experiments_testing_feature_ids() -> String {
    use serde_json::json;
    json!({
        "data": [
            {
                "schemaVersion": "1.0.0",
                "slug": "startup-gold",
                "endDate": null,
                "featureIds": ["aboutmonkeys"],
                "branches":[
                    {
                        "slug": "control",
                        "ratio": 1,
                        "feature": {
                            "featureId": "aboutmonkeys",
                            "enabled": false
                        }
                    },
                    {
                        "slug": "treatment",
                        "ratio":1,
                        "feature": {
                            "featureId": "aboutmonkeys",
                            "enabled": true
                        },

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
                    "namespace":"startup-gold",
                    "randomizationUnit":"nimbus_id"
                },
                "userFacingName":"Diagnostic test experiment",
                "referenceBranch":"control",
                "isEnrollmentPaused":false,
                "proposedEnrollment":7,
                "userFacingDescription":"This is a test experiment for diagnostic purposes.",
                "id":"startup-gold",
                "last_modified":1_602_197_324_372i64
            },
            {
                "schemaVersion": "1.0.0",
                "slug": "secure-gold",
                "endDate": null,
                "featureIds": ["aboutwelcome"],
                "branches":[
                    {
                        "slug": "control",
                        "ratio": 1,
                        "feature": {
                            "featureId": "aboutwelcome",
                            "enabled": false
                        },
                    },
                    {
                        "slug": "treatment",
                        "ratio":1,
                        "feature": {
                            "featureId": "aboutwelcome",
                            "enabled": true
                        },
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
                    "namespace":"secure-gold",
                    "randomizationUnit":"nimbus_id"
                },
                "userFacingName":"Diagnostic test experiment",
                "referenceBranch":"control",
                "isEnrollmentPaused":false,
                "proposedEnrollment":7,
                "userFacingDescription":"This is a test experiment for diagnostic purposes.",
                "id":"secure-gold",
                "last_modified":1_602_197_324_372i64
            },
            {
                "schemaVersion": "1.0.0",
                "slug": "no-features",
                "endDate": null,
                "branches":[
                    {
                        "slug": "control",
                        "ratio": 1,
                    },
                    {
                        "slug": "treatment",
                        "ratio": 1,
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
                    "namespace":"secure-gold",
                    "randomizationUnit":"nimbus_id"
                },
                "userFacingName":"Diagnostic test experiment",
                "referenceBranch":"control",
                "isEnrollmentPaused":false,
                "proposedEnrollment":7,
                "userFacingDescription":"This is a test experiment for diagnostic purposes.",
                "id":"no-features",
                "last_modified":1_602_197_324_372i64
            }
        ]
    })
    .to_string()
}

#[allow(dead_code)] // work around https://github.com/rust-lang/rust/issues/46379
pub fn no_test_experiments() -> String {
    use serde_json::json;
    json!({
        "data": []
    })
    .to_string()
}
