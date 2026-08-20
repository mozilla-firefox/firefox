/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#![allow(unexpected_cfgs)]

#[cfg(feature = "stateful")]
use std::collections::HashMap;
use std::collections::HashSet;
use std::sync::{Arc, Mutex};

use serde::Serialize;
#[cfg(feature = "stateful")]
use serde_json::Map;
use serde_json::{Value, json};

pub use self::detail::*;
use crate::enrollment::{
    EnrolledFeatureConfig, EnrolledReason, EnrollmentChangeEvent, ExperimentEnrollment,
    NotEnrolledReason,
};
#[cfg(feature = "stateful")]
use crate::json::JsonObject;
use crate::metrics::EnrollmentStatusExtraDef;
#[cfg(feature = "stateful")]
use crate::stateful::behavior::EventStore;
#[cfg(feature = "stateful")]
use crate::stateful::gecko_prefs::OriginalGeckoPref;
#[cfg(feature = "stateful")]
use crate::stateful::gecko_prefs::{
    GeckoPrefHandler, GeckoPrefState, MapOfFeatureIdToPropertyNameToGeckoPrefState,
};
#[cfg(feature = "stateful")]
use crate::stateful::targeting::RecordedContext;
use crate::{
    AppContext, EnrollmentStatus, Experiment, FeatureConfig, NimbusTargetingHelper,
    TargetingAttributes,
};

#[ctor::ctor(unsafe)]
fn init() {
    error_support::init_for_tests_with_level(error_support::Level::Info);
}

impl From<TargetingAttributes> for NimbusTargetingHelper {
    fn from(value: TargetingAttributes) -> Self {
        cfg_if::cfg_if! {
            if #[cfg(feature = "stateful")] {
                let store = Arc::new(Mutex::new(EventStore::new()));
                NimbusTargetingHelper::new(value, store, None)
            } else {
                NimbusTargetingHelper::new(value)
            }
        }
    }
}

impl Default for NimbusTargetingHelper {
    fn default() -> Self {
        cfg_if::cfg_if! {
            if #[cfg(feature = "stateful")] {
                let ctx: AppContext = Default::default();
                let store = Arc::new(Mutex::new(EventStore::new()));
                NimbusTargetingHelper::new(ctx, store, None)
            } else {
                let ctx: AppContext = Default::default();
                NimbusTargetingHelper::new(ctx)
            }
        }
    }
}

#[cfg(feature = "stateful")]
#[derive(Default)]
struct RecordedContextState {
    context: Map<String, Value>,
    record_calls: u64,
    event_queries: HashMap<String, String>,
    event_query_values: HashMap<String, f64>,
}

#[cfg(feature = "stateful")]
#[derive(Default)]
pub struct TestRecordedContext {
    state: Mutex<RecordedContextState>,
}

#[cfg(feature = "stateful")]
impl TestRecordedContext {
    pub fn new() -> Arc<Self> {
        Arc::new(TestRecordedContext {
            state: Default::default(),
        })
    }

    pub fn get_record_calls(&self) -> u64 {
        self.state
            .lock()
            .expect("could not lock state mutex")
            .record_calls
    }

    pub fn set_context(&self, context: Value) {
        let mut state = self.state.lock().expect("could not lock state mutex");
        state.context = context
            .as_object()
            .expect("value for `context` is not an object")
            .clone();
    }

    pub fn set_event_queries(&self, queries: HashMap<String, String>) {
        let mut state = self.state.lock().expect("could not lock state mutex");
        state.event_queries = queries;
    }

    pub fn get_event_query_values(&self) -> HashMap<String, f64> {
        self.state
            .lock()
            .expect("could not lock state mutex")
            .event_query_values
            .clone()
    }
}

#[cfg(feature = "stateful")]
impl RecordedContext for TestRecordedContext {
    fn to_json(&self) -> JsonObject {
        self.state
            .lock()
            .expect("could not lock state mutex")
            .context
            .clone()
    }

    fn get_event_queries(&self) -> HashMap<String, String> {
        self.state
            .lock()
            .expect("could not lock state mutex")
            .event_queries
            .clone()
    }

    fn set_event_query_values(&self, event_query_values: HashMap<String, f64>) {
        let mut state = self.state.lock().expect("could not lock state mutex");
        state.event_query_values.clone_from(&event_query_values);
        state
            .context
            .insert("events".into(), json!(event_query_values));
    }

    fn record(&self) {
        let mut state = self.state.lock().expect("could not lock state mutex");
        state.record_calls += 1;
    }
}

/// A Rust implementation of the MetricsHandler trait
/// Used to test recording of Glean metrics across the FFI within Rust
///
/// *NOTE: Use this struct's `new` method when instantiating it to lock the Glean store*
pub struct TestMetrics {
    state: Mutex<MetricState>,
}

impl TestMetrics {
    pub fn new() -> Arc<Self> {
        Arc::new(TestMetrics {
            state: Default::default(),
        })
    }

    pub fn get_enrollment_statuses(&self) -> Vec<EnrollmentStatusExtraDef> {
        self.state.lock().unwrap().enrollment_statuses.clone()
    }
}

#[cfg(feature = "stateful")]
pub struct TestGeckoPrefHandlerState {
    pub prefs_set: Option<Vec<GeckoPrefState>>,
    pub original_prefs_state: Option<Vec<OriginalGeckoPref>>,
    pub set_gecko_prefs_state_call_count: u32,
}

#[cfg(feature = "stateful")]
pub struct TestGeckoPrefHandler {
    pub prefs: MapOfFeatureIdToPropertyNameToGeckoPrefState,
    pub state: Mutex<TestGeckoPrefHandlerState>,
}

#[cfg(feature = "stateful")]
impl TestGeckoPrefHandler {
    pub(crate) fn new(prefs: MapOfFeatureIdToPropertyNameToGeckoPrefState) -> Arc<Self> {
        Arc::new(Self {
            prefs,
            state: Mutex::new(TestGeckoPrefHandlerState {
                prefs_set: None,
                original_prefs_state: None,
                set_gecko_prefs_state_call_count: 0,
            }),
        })
    }
}

#[cfg(feature = "stateful")]
impl GeckoPrefHandler for TestGeckoPrefHandler {
    fn get_prefs_with_state(&self) -> MapOfFeatureIdToPropertyNameToGeckoPrefState {
        self.prefs.clone()
    }

    fn set_gecko_prefs_state(&self, new_prefs_state: Vec<GeckoPrefState>) {
        let mut state = self
            .state
            .lock()
            .expect("Unable to lock TestGeckoPrefHandler state");
        state.prefs_set = Some(new_prefs_state);
        state.set_gecko_prefs_state_call_count += 1;
    }

    fn set_gecko_prefs_original_values(&self, original_prefs_state: Vec<OriginalGeckoPref>) {
        self.state
            .lock()
            .expect("Unable to lock TestGeckoPrefHandler state")
            .original_prefs_state = Some(original_prefs_state);
    }
}

pub(crate) fn get_test_experiments() -> Vec<Experiment> {
    vec![
        serde_json::from_value(json!({
            "schemaVersion": "1.0.0",
            "slug": "secure-gold",
            "endDate": null,
            "featureIds": ["some_control"],
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {
                        "featureId": "some_control",
                        "enabled": false,
                        "value": {
                            "text": "OK then",
                            "number": 42
                        }
                    }
                },
                {
                    "slug": "treatment",
                    "ratio":1,
                    "feature": {
                        "featureId": "some_control",
                        "enabled": true,
                        "value": {
                            "text": "OK then",
                            "number": 42
                        }
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
        }))
        .unwrap(),
        serde_json::from_value(json!({
            "schemaVersion": "1.0.0",
            "slug": "secure-silver",
            "endDate": null,
            "branches":[
                {
                    "slug": "control",
                    "ratio": 1,
                    "feature": {
                        "featureId": "about_welcome",
                        "enabled": true,
                    }
                },
                {
                    "slug": "treatment",
                    "ratio": 1,
                    "feature": {
                        "featureId": "about_welcome",
                        "enabled": false,
                    }
                },
            ],
            "featureIds": ["about_welcome"],
            "channel": "nightly",
            "probeSets":[],
            "startDate":null,
            "appName":"fenix",
            "appId":"org.mozilla.fenix",
            "bucketConfig":{
                // Also enroll everyone.
                "count":10_000,
                "start":0,
                "total":10_000,
                "namespace":"secure-silver",
                "randomizationUnit":"nimbus_id"
            },
            "userFacingName":"2nd test experiment",
            "referenceBranch":"control",
            "isEnrollmentPaused":false,
            "proposedEnrollment":7,
            "userFacingDescription":"2nd test experiment.",
            "id":"secure-silver",
            "last_modified":1_602_197_324_372i64
        }))
        .unwrap(),
    ]
}

#[cfg(feature = "stateful")]
pub fn get_ios_rollout_experiment() -> Experiment {
    serde_json::from_value(json!(
    {
      "appId": "org.mozilla.ios.Firefox",
      "appName": "firefox_ios",
      "application": "org.mozilla.ios.Firefox",
      "arguments": {},
      "branches": [
        {
          "feature": {
            "enabled": true,
            "featureId": "coordinators-refactor-feature",
            "value": {
              "enabled": true
            }
          },
          "ratio": 1,
          "slug": "control"
        }
      ],
      "bucketConfig": {
        "count": 10000,
        "namespace": "ios-coordinators-refactor-feature-release-no_targeting-rollout-1",
        "randomizationUnit": "nimbus_id",
        "start": 0,
        "total": 10000
      },
      "channel": "release",
      "endDate": null,
      "enrollmentEndDate": null,
      "featureIds": [
        "coordinators-refactor-feature"
      ],
      "featureValidationOptOut": false,
      "id": "ios-coordinators-rollout",
      "isEnrollmentPaused": false,
      "isRollout": true,
      "localizations": null,
      "outcomes": [],
      "probeSets": [],
      "proposedDuration": 28,
      "proposedEnrollment": 7,
      "referenceBranch": "control",
      "schemaVersion": "1.12.0",
      "slug": "ios-coordinators-rollout",
      "startDate": null,
      "targeting": "(app_version|versionCompare('114.!') >= 0)",
      "userFacingDescription": "Rollout of coordinators refactor.",
      "userFacingName": "iOS Coordinators Rollout"
    }))
    .unwrap()
}

impl FeatureConfig {
    fn new(feature_id: &str, value: Value) -> Self {
        Self {
            feature_id: feature_id.to_string(),
            value: value.as_object().unwrap().to_owned(),
        }
    }
}

impl EnrolledFeatureConfig {
    pub(crate) fn new(feature_id: &str, value: Value, exp: &str, branch: Option<&str>) -> Self {
        Self {
            feature_id: feature_id.to_string(),
            feature: FeatureConfig::new(feature_id, value),
            slug: exp.to_string(),
            branch: branch.map(ToString::to_string),
        }
    }
}

// Helper constructors for enrollments.
impl ExperimentEnrollment {
    pub(crate) fn enrolled(slug: &str) -> Self {
        Self {
            slug: slug.to_string(),
            status: EnrollmentStatus::Enrolled {
                branch: "control".to_string(),
                reason: EnrolledReason::Qualified,
                #[cfg(feature = "stateful")]
                prev_gecko_pref_states: None,
            },
        }
    }
    pub(crate) fn not_enrolled(slug: &str) -> Self {
        Self {
            slug: slug.to_string(),
            status: EnrollmentStatus::NotEnrolled {
                reason: NotEnrolledReason::NotSelected,
            },
        }
    }
}

pub fn get_single_feature_experiment(slug: &str, feature_id: &str, config: Value) -> Experiment {
    serde_json::from_value(json!(
        {
        "schemaVersion": "1.0.0",
        "slug": slug,
        "endDate": null,
        "branches":[
            {
                "slug": "control",
                "ratio": 1,
                "feature": {
                    "featureId": feature_id,
                    "enabled": true,
                    "value": config,
                }
            },
        ],
        "featureIds": [feature_id],
        "channel": "nightly",
        "probeSets":[],
        "startDate":null,"appName":"fenix",
        "appId":"org.mozilla.fenix",
        "bucketConfig":{
            // Also enroll everyone.
            "count":10_000,
            "start":0,
            "total":10_000,
            "namespace":"secure-silver",
            "randomizationUnit":"nimbus_id"
        },
        "userFacingName":"",
        "referenceBranch":"control",
        "isEnrollmentPaused":false,
        "proposedEnrollment":7,
        "userFacingDescription":"",
    }
    ))
    .unwrap()
}

#[cfg(feature = "stateful")]
pub fn get_single_feature_rollout(slug: &str, feature_id: &str, config: Value) -> Experiment {
    serde_json::from_value(json!(
        {
        "schemaVersion": "1.0.0",
        "slug": slug,
        "endDate": null,
        "branches":[
            {
                "slug": "control",
                "ratio": 1,
                "feature": {
                    "featureId": feature_id,
                    "enabled": true,
                    "value": config,
                }
            },
        ],
        "featureIds": [feature_id],
        "channel": "nightly",
        "probeSets":[],
        "startDate":null,"appName":"fenix",
        "appId":"org.mozilla.fenix",
        "bucketConfig":{
            // Also enroll everyone.
            "count":10_000,
            "start":0,
            "total":10_000,
            "namespace":"secure-silver",
            "randomizationUnit":"nimbus_id"
        },
        "userFacingName":"",
        "referenceBranch":"control",
        "isEnrollmentPaused":false,
        "isRollout": true,
        "proposedEnrollment":7,
        "userFacingDescription":"",
    }
    ))
    .unwrap()
}

pub fn get_bucketed_rollout(slug: &str, count: i64) -> Experiment {
    get_bucketed_rollout_with_feature(slug, count, "a-feature")
}

pub fn get_bucketed_rollout_with_feature(slug: &str, count: i64, feature_id: &str) -> Experiment {
    serde_json::from_value(json!(
        {
        "schemaVersion": "1.0.0",
        "slug": slug,
        "endDate": null,
        "branches":[
            {
                "slug": "control",
                "ratio": 1,
                "feature": {
                    "featureId": feature_id,
                    "enabled": true,
                    "value": {},
                }
            },
        ],
        "isRollout": true,
        "featureIds": [feature_id],
        "channel": "nightly",
        "probeSets": [],
        "startDate": null,
        "appName":"fenix",
        "appId":"org.mozilla.fenix",
        "bucketConfig":{
            "count": count,
            "start": 0,
            "total": 10_000,
            "namespace":"secure-silver",
            "randomizationUnit":"nimbus_id"
        },
        "userFacingName":"",
        "referenceBranch":"control",
        "isEnrollmentPaused":false,
        "proposedEnrollment":7,
        "userFacingDescription":"",
    }
    ))
    .unwrap()
}

pub fn get_multi_feature_experiment(slug: &str, features: Vec<(&str, Value)>) -> Experiment {
    let remapped_features = Value::Array(
        features
            .clone()
            .into_iter()
            .map(|(f, v)| {
                json!({
                    "featureId": f,
                    "enabled": true,
                    "value": v,
                })
            })
            .collect(),
    );
    serde_json::from_value(json!(
        {
        "schemaVersion": "1.0.0",
        "slug": slug,
        "endDate": null,
        "branches":[
            {
                "slug": "control",
                "ratio": 1,
                "features": remapped_features,
            },
        ],
        "featureIds": Value::Array(features.iter().map(|(f, _)| Value::String(f.to_string())).collect::<Vec<Value>>()),
        "channel": "nightly",
        "probeSets":[],
        "startDate":null,
        "appName":"fenix",
        "appId":"org.mozilla.fenix",
        "bucketConfig":{
            // Also enroll everyone.
            "count":10_000,
            "start":0,
            "total":10_000,
            "namespace":"secure-silver",
            "randomizationUnit":"nimbus_id"
        },
        "userFacingName":"",
        "referenceBranch":"control",
        "isEnrollmentPaused":false,
        "proposedEnrollment":7,
        "userFacingDescription":"",
    }
    ))
    .unwrap()
}

impl Experiment {
    pub fn with_targeting(mut self, targeting: &str) -> Self {
        self.targeting = Some(targeting.into());
        self
    }
}

pub fn no_coenrolling_features() -> HashSet<&'static str> {
    Default::default()
}

#[cfg_attr(not(feature = "stateful"), allow(unused))]
pub fn to_local_experiments_string<S>(experiments: &[S]) -> crate::Result<String>
where
    S: Serialize,
{
    Ok(serde_json::to_string(&json!({ "data": experiments }))?)
}

#[cfg_attr(not(feature = "stateful"), allow(unused))]
pub fn get_targeted_experiment(slug: &str, targeting: &str) -> serde_json::Value {
    get_targeted_experiment_with_feature(slug, targeting, "some-feature-1")
}

#[cfg_attr(not(feature = "stateful"), allow(unused))]
pub fn get_targeted_experiment_with_feature(
    slug: &str,
    targeting: &str,
    feature_id: &str,
) -> serde_json::Value {
    json!({
        "schemaVersion": "1.0.0",
        "slug": slug,
        "endDate": null,
        "featureIds": [feature_id],
        "branches": [
            {
                "slug": "control",
                "ratio": 1,
                "features": [
                    {
                        "featureId": feature_id,
                        "value": {}
                    }
                ]
            },
            {
                "slug": "treatment",
                "ratio": 1,
                "features": [
                    {
                        "featureId": feature_id,
                        "value": {}
                    }
                ]
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
            "namespace": "secure-gold",
            "randomizationUnit": "nimbus_id"
        },
        "targeting": targeting,
        "userFacingName": "test experiment",
        "referenceBranch": "control",
        "isEnrollmentPaused": false,
        "proposedEnrollment": 7,
        "userFacingDescription": "This is a test experiment for testing purposes.",
        "id": "secure-copper",
        "last_modified": 1_602_197_324_372i64,
    })
}

pub(crate) fn get_experiment_with_published_date(
    slug: &str,
    published_date: Option<String>,
) -> Experiment {
    serde_json::from_value(json!({
        "schemaVersion": "1.0.0",
        "slug": slug,
        "endDate": null,
        "branches":[
            {
                "slug": "control",
                "ratio": 1,
                "feature": {
                    "featureId": "about_welcome",
                    "enabled": true,
                }
            },
            {
                "slug": "treatment",
                "ratio": 1,
                "feature": {
                    "featureId": "about_welcome",
                    "enabled": false,
                }
            },
        ],
        "featureIds": ["about_welcome"],
        "channel": "nightly",
        "probeSets":[],
        "startDate":null,
        "appName":"fenix",
        "appId":"org.mozilla.fenix",
        "bucketConfig":{
            // Also enroll everyone.
            "count":10_000,
            "start":0,
            "total":10_000,
            "namespace":"secure-silver",
            "randomizationUnit":"nimbus_id"
        },
        "userFacingName":"2nd test experiment",
        "referenceBranch":"control",
        "isEnrollmentPaused":false,
        "proposedEnrollment":7,
        "userFacingDescription":"2nd test experiment.",
        "id":"secure-silver",
        "last_modified":1_602_197_324_372i64,
        "publishedDate": published_date
    }))
    .unwrap()
}

#[allow(unused)]
pub(crate) fn get_firefox_lab(slug: &str) -> Experiment {
    get_firefox_lab_with_feature(slug, "labs-feature")
}

#[allow(unused)]
pub(crate) fn get_firefox_lab_with_feature(slug: &str, feature_id: &str) -> Experiment {
    serde_json::from_value(json!({
        "schemaVersion": "1.0.0",
        "appName": "fenix",
        "appId": "org.mozilla.fenix",
        "channel": "nightly",
        "slug": slug,
        "branches": [
            {
                "slug": "control",
                "ratio": 1,
                "features": [
                    {
                        "featureId": feature_id,
                        "value": {}
                    }
                ]
            }
        ],
        "featureIds": [feature_id],
        "isRollout": true,
        "isEnrollmentPaused": false,
        "isFirefoxLabsOptIn": true,
        "firefoxLabsTitle": "labs-title",
        "firefoxLabsDescription": "labs-description",
        "firefoxLabsDescriptionLinks": {
            "feedback": "https://example.com/#feedback",
            "learn-more": "https://example.com/#learn-more",
        },
        "requiresRestart": false,
        "userFacingName": "Test Firefox Labs",
        "userFacingDescription": "Test Firefox Labs",
        "bucketConfig": {
            "randomizationUnit": "nimbus_id",
            "start": 0,
            "count": 10000,
            "total": 10000,
            "namespace": "firefox-labs-test"
        },
        "targeting": "true",
        "proposedEnrollment": 0,
    }))
    .unwrap()
}

#[cfg(feature = "stateful")]
mod detail {
    use super::TestMetrics;
    use crate::metrics::{
        DatabaseLoadExtraDef, DatabaseMigrationExtraDef, EnrollmentStatusExtraDef,
        FeatureExposureExtraDef, MalformedFeatureConfigExtraDef, MetricsHandler,
    };

    #[derive(Clone, Default)]
    pub struct MetricState {
        pub activations: Vec<FeatureExposureExtraDef>,
        pub database_load_events: Vec<DatabaseLoadExtraDef>,
        pub database_migration_events: Vec<DatabaseMigrationExtraDef>,
        pub enrollment_statuses: Vec<EnrollmentStatusExtraDef>,
        pub exposures: Vec<FeatureExposureExtraDef>,
        pub malformeds: Vec<MalformedFeatureConfigExtraDef>,
        pub submit_targeting_context_calls: u64,
    }

    impl TestMetrics {
        pub fn get_database_load_events(&self) -> Vec<DatabaseLoadExtraDef> {
            self.state.lock().unwrap().database_load_events.clone()
        }

        pub fn get_database_migration_events(&self) -> Vec<DatabaseMigrationExtraDef> {
            self.state.lock().unwrap().database_migration_events.clone()
        }

        pub fn get_activations(&self) -> Vec<FeatureExposureExtraDef> {
            self.state.lock().unwrap().activations.clone()
        }

        pub fn get_malformeds(&self) -> Vec<MalformedFeatureConfigExtraDef> {
            self.state.lock().unwrap().malformeds.clone()
        }

        pub fn get_submit_targeting_context_calls(&self) -> u64 {
            self.state.lock().unwrap().submit_targeting_context_calls
        }

        pub fn clear(&self) {
            std::mem::take(&mut *self.state.lock().unwrap());
        }
    }

    impl MetricsHandler for TestMetrics {
        fn record_database_load(&self, event: DatabaseLoadExtraDef) {
            let mut state = self.state.lock().unwrap();
            state.database_load_events.push(event);
        }

        fn record_database_migration(&self, event: DatabaseMigrationExtraDef) {
            let mut state = self.state.lock().unwrap();
            state.database_migration_events.push(event);
        }

        fn record_enrollment_statuses(
            &self,
            enrollment_status_extras: Vec<EnrollmentStatusExtraDef>,
        ) {
            let mut state = self.state.lock().unwrap();
            state.enrollment_statuses.extend(enrollment_status_extras);
        }

        fn record_feature_activation(&self, event: FeatureExposureExtraDef) {
            let mut state = self.state.lock().unwrap();
            state.activations.push(event);
        }

        fn record_feature_exposure(&self, event: FeatureExposureExtraDef) {
            let mut state = self.state.lock().unwrap();
            state.exposures.push(event);
        }

        fn record_malformed_feature_config(&self, event: MalformedFeatureConfigExtraDef) {
            let mut state = self.state.lock().unwrap();
            state.malformeds.push(event);
        }

        fn submit_targeting_context(&self) {
            let mut state = self.state.lock().unwrap();
            state.submit_targeting_context_calls += 1;
        }
    }
}

#[cfg(not(feature = "stateful"))]
mod detail {
    use super::TestMetrics;
    use crate::metrics::{EnrollmentStatusExtraDef, MetricsHandler};

    #[derive(Clone, Default)]
    pub struct MetricState {
        pub enrollment_statuses: Vec<EnrollmentStatusExtraDef>,
        pub nimbus_user_id: Option<String>,
    }

    impl TestMetrics {
        pub fn get_nimbus_user_id(&self) -> Option<String> {
            self.state.lock().unwrap().nimbus_user_id.clone()
        }
    }

    impl MetricsHandler for TestMetrics {
        fn record_enrollment_statuses_v2(
            &self,
            enrollment_status_extras: Vec<EnrollmentStatusExtraDef>,
            nimbus_user_id: Option<String>,
        ) {
            let mut state = self.state.lock().unwrap();
            state.enrollment_statuses.extend(enrollment_status_extras);
            state.nimbus_user_id = nimbus_user_id;
        }
    }
}

/// Return the given enrollments in a sorted order.
pub fn sorted_enrollment_change_events(
    mut events: Vec<EnrollmentChangeEvent>,
) -> Vec<EnrollmentChangeEvent> {
    events.sort_by(|e1, e2| e1.experiment_slug.cmp(&e2.experiment_slug));

    events
}
