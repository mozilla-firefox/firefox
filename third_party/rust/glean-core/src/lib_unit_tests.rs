// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// NOTE: This is a test-only file that contains unit tests for
// the lib.rs file.

use std::collections::HashSet;

use internal_pings::InternalPings;
use serde_json::json;

use super::*;

const GLOBAL_APPLICATION_ID: &str = "org.mozilla.glean.test.app";
pub fn new_glean(tempdir: Option<tempfile::TempDir>) -> (Glean, tempfile::TempDir) {
    let _ = env_logger::builder().try_init();
    let dir = match tempdir {
        Some(tempdir) => tempdir,
        None => tempfile::tempdir().unwrap(),
    };
    let tmpname = dir.path().display().to_string();
    let mut glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
    // Register the builtin pings as enabled.
    _ = InternalPings::new(true);

    // store{1, 2} is used throughout tests
    let ping = PingType::new_internal(
        "store1",
        true,
        false,
        true,
        true,
        true,
        vec![],
        vec![],
        true,
        vec![],
    );
    glean.register_ping_type(&ping);
    let ping = PingType::new_internal(
        "store2",
        true,
        false,
        true,
        true,
        true,
        vec![],
        vec![],
        true,
        vec![],
    );
    glean.register_ping_type(&ping);
    (glean, dir)
}

#[test]
fn path_is_constructed_from_data() {
    let (glean, _t) = new_glean(None);

    assert_eq!(
        "/submit/org-mozilla-glean-test-app/baseline/1/this-is-a-docid",
        glean.make_path("baseline", "this-is-a-docid")
    );
}

// Experiment's API tests: the next two tests come from glean-ac's
// ExperimentsStorageEngineTest.kt.
#[test]
fn experiment_id_and_branch_get_truncated_if_too_long() {
    let t = tempfile::tempdir().unwrap();
    let name = t.path().display().to_string();
    let glean = Glean::with_options(&name, "org.mozilla.glean.tests", true, true);

    // Generate long strings for the used ids.
    let very_long_id = "test-experiment-id".repeat(10);
    let very_long_branch_id = "test-branch-id".repeat(10);

    // Mark the experiment as active.
    glean.set_experiment_active(
        very_long_id.clone(),
        very_long_branch_id.clone(),
        HashMap::new(),
    );

    // Generate the expected id and branch strings.
    let mut expected_id = very_long_id;
    expected_id.truncate(100);
    let mut expected_branch_id = very_long_branch_id;
    expected_branch_id.truncate(100);

    assert!(
        glean
            .test_get_experiment_data(expected_id.clone())
            .is_some(),
        "An experiment with the truncated id should be available"
    );

    // Make sure the branch id was truncated as well.
    let experiment_data = glean.test_get_experiment_data(expected_id);
    assert!(
        experiment_data.is_some(),
        "Experiment data must be available"
    );

    let experiment_data = experiment_data.unwrap();
    assert_eq!(expected_branch_id, experiment_data.branch);
}

#[test]
fn limits_on_experiments_extras_are_applied_correctly() {
    let t = tempfile::tempdir().unwrap();
    let name = t.path().display().to_string();
    let glean = Glean::with_options(&name, "org.mozilla.glean.tests", true, true);

    let experiment_id = "test-experiment_id".to_string();
    let branch_id = "test-branch-id".to_string();
    let mut extras = HashMap::new();

    let too_long_key = "0123456789".repeat(11);
    let too_long_value = "0123456789".repeat(11);

    // Build and extras HashMap that's a little too long in every way
    for n in 0..21 {
        extras.insert(format!("{}-{}", n, too_long_key), too_long_value.clone());
    }

    // Mark the experiment as active.
    glean.set_experiment_active(experiment_id.clone(), branch_id, extras);

    // Make sure it is active
    assert!(
        glean
            .test_get_experiment_data(experiment_id.clone())
            .is_some(),
        "An experiment with the truncated id should be available"
    );

    // Get the data
    let experiment_data = glean.test_get_experiment_data(experiment_id);
    assert!(
        experiment_data.is_some(),
        "Experiment data must be available"
    );

    // Parse the JSON and validate the lengths
    let experiment_data = experiment_data.unwrap();
    assert_eq!(
        20,
        experiment_data.extra.as_ref().unwrap().len(),
        "Experiments extra must be less than max length"
    );

    for (key, value) in experiment_data.extra.as_ref().unwrap().iter() {
        assert!(
            key.len() <= 100,
            "Experiments extra key must be less than max length"
        );
        assert!(
            value.len() <= 100,
            "Experiments extra value must be less than max length"
        );
    }
}

#[test]
fn experiments_status_is_correctly_toggled() {
    let t = tempfile::tempdir().unwrap();
    let name = t.path().display().to_string();
    let glean = Glean::with_options(&name, "org.mozilla.glean.tests", true, true);

    // Define the experiment's data.
    let experiment_id: String = "test-toggle-experiment".into();
    let branch_id: String = "test-branch-toggle".into();
    let extra: HashMap<String, String> = [("test-key".into(), "test-value".into())]
        .iter()
        .cloned()
        .collect();

    // Activate an experiment.
    glean.set_experiment_active(experiment_id.clone(), branch_id, extra.clone());

    // Check that the experiment is marekd as active.
    assert!(
        glean
            .test_get_experiment_data(experiment_id.clone())
            .is_some(),
        "The experiment must be marked as active."
    );

    // Check that the extra data was stored.
    let experiment_data = glean.test_get_experiment_data(experiment_id.clone());
    assert!(
        experiment_data.is_some(),
        "Experiment data must be available"
    );

    let experiment_data = experiment_data.unwrap();
    assert_eq!(experiment_data.extra.unwrap(), extra);

    // Disable the experiment and check that is no longer available.
    glean.set_experiment_inactive(experiment_id.clone());
    assert!(
        glean.test_get_experiment_data(experiment_id).is_none(),
        "The experiment must not be available any more."
    );
}

#[test]
fn experimentation_id_is_set_correctly() {
    let t = tempfile::tempdir().unwrap();
    let name = t.path().display().to_string();

    // Define an experimentation id to test
    let experimentation_id = "test-experimentation-id";

    let glean = Glean::new(InternalConfiguration {
        data_path: name,
        application_id: GLOBAL_APPLICATION_ID.into(),
        language_binding_name: "Rust".into(),
        upload_enabled: true,
        max_events: None,
        delay_ping_lifetime_io: false,
        app_build: "Unknown".into(),
        use_core_mps: false,
        trim_data_to_registered_pings: false,
        log_level: None,
        rate_limit: None,
        enable_event_timestamps: true,
        experimentation_id: Some(experimentation_id.to_string()),
        enable_internal_pings: true,
        ping_schedule: Default::default(),
        ping_lifetime_threshold: 0,
        ping_lifetime_max_time: 0,
    })
    .unwrap();

    // Check that the correct value was stored
    if let Some(exp_id) = glean
        .additional_metrics
        .experimentation_id
        .get_value(&glean, "all-pings")
    {
        assert_eq!(exp_id, experimentation_id, "Experimentation ids must match");
    } else {
        panic!("The experimentation id must not be `None`");
    }
}

#[test]
fn client_id_and_first_run_date_must_be_regenerated() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    {
        let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);

        glean.data_store.as_ref().unwrap().clear_all();

        assert!(glean
            .core_metrics
            .client_id
            .get_value(&glean, "glean_client_info")
            .is_none());
        assert!(glean
            .core_metrics
            .first_run_date
            .get_value(&glean, "glean_client_info")
            .is_none());
    }

    {
        let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
        assert!(glean
            .core_metrics
            .client_id
            .get_value(&glean, "glean_client_info")
            .is_some());
        assert!(glean
            .core_metrics
            .first_run_date
            .get_value(&glean, "glean_client_info")
            .is_some());
    }
}

#[test]
fn basic_metrics_should_be_cleared_when_uploading_is_disabled() {
    let (mut glean, _t) = new_glean(None);
    let metric = StringMetric::new(CommonMetricData {
        category: "category".to_string(),
        name: "string_metric".to_string(),
        send_in_pings: vec!["baseline".to_string()],
        ..Default::default()
    });

    metric.set_sync(&glean, "TEST VALUE");
    assert!(metric.get_value(&glean, "baseline").is_some());

    glean.set_upload_enabled(false);
    assert!(metric.get_value(&glean, "baseline").is_none());

    metric.set_sync(&glean, "TEST VALUE");
    assert!(metric.get_value(&glean, "baseline").is_none());

    glean.set_upload_enabled(true);
    assert!(metric.get_value(&glean, "baseline").is_none());

    metric.set_sync(&glean, "TEST VALUE");
    assert!(metric.get_value(&glean, "baseline").is_some());
}

#[test]
fn first_run_date_is_managed_correctly_when_toggling_uploading() {
    let (mut glean, _t) = new_glean(None);

    let original_first_run_date = glean
        .core_metrics
        .first_run_date
        .get_value(&glean, "glean_client_info");

    glean.set_upload_enabled(false);
    assert_eq!(
        original_first_run_date,
        glean
            .core_metrics
            .first_run_date
            .get_value(&glean, "glean_client_info")
    );

    glean.set_upload_enabled(true);
    assert_eq!(
        original_first_run_date,
        glean
            .core_metrics
            .first_run_date
            .get_value(&glean, "glean_client_info")
    );
}

#[test]
fn client_id_is_managed_correctly_when_toggling_uploading() {
    let (mut glean, _t) = new_glean(None);

    let original_client_id = glean
        .core_metrics
        .client_id
        .get_value(&glean, "glean_client_info");
    assert!(original_client_id.is_some());
    assert_ne!(*KNOWN_CLIENT_ID, original_client_id.unwrap());

    glean.set_upload_enabled(false);
    assert_eq!(
        None,
        glean
            .core_metrics
            .client_id
            .get_value(&glean, "glean_client_info")
    );

    glean.set_upload_enabled(true);
    let current_client_id = glean
        .core_metrics
        .client_id
        .get_value(&glean, "glean_client_info");
    assert!(current_client_id.is_some());
    assert_ne!(*KNOWN_CLIENT_ID, current_client_id.unwrap());
    assert_ne!(original_client_id, current_client_id);
}

#[test]
fn client_id_is_not_set_when_uploading_disabled_at_start() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, false, true);

    assert_eq!(
        None,
        glean
            .core_metrics
            .client_id
            .get_value(&glean, "glean_client_info")
    );
}

#[test]
fn client_id_is_set_to_random_value_when_uploading_enabled_at_start() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);

    let current_client_id = glean
        .core_metrics
        .client_id
        .get_value(&glean, "glean_client_info");
    assert!(current_client_id.is_some());
    assert_ne!(*KNOWN_CLIENT_ID, current_client_id.unwrap());
}

#[test]
fn attribution_and_distribution_are_correctly_stored() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);

    // On a fresh Glean, no attribution or distribution information is set.
    assert_eq!(
        <AttributionMetrics as Default>::default(),
        glean.test_get_attribution()
    );
    assert_eq!(
        <DistributionMetrics as Default>::default(),
        glean.test_get_distribution()
    );

    let mut attribution = AttributionMetrics {
        source: Some("source".into()),
        medium: Some("medium".into()),
        campaign: Some("campaign".into()),
        term: Some("term".into()),
        content: Some("content".into()),
    };
    let distribution = DistributionMetrics {
        name: Some("name".into()),
    };

    // Set them all at once.
    glean.update_attribution(attribution.clone());
    glean.update_distribution(distribution.clone());

    assert_eq!(attribution, glean.test_get_attribution());
    assert_eq!(distribution, glean.test_get_distribution());

    let attribution_update = AttributionMetrics {
        campaign: Some("new campaign".into()),
        ..Default::default()
    };
    let distribution_update = DistributionMetrics {
        name: Some("new name".into()),
    };

    // Perform updates.
    glean.update_attribution(attribution_update);
    glean.update_distribution(distribution_update.clone());

    // Ensure only the updated fields took over
    attribution.campaign = Some("new campaign".into());
    assert_eq!(attribution, glean.test_get_attribution());
    assert_eq!(distribution_update, glean.test_get_distribution());
}

#[test]
fn enabling_when_already_enabled_is_a_noop() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    let mut glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);

    assert!(!glean.set_upload_enabled(true));
}

#[test]
fn disabling_when_already_disabled_is_a_noop() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    let mut glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, false, true);

    assert!(!glean.set_upload_enabled(false));
}

// Test that the enum variants keep a stable discriminant when serialized.
// Discriminant values are taken from a stable ordering from v20.0.0.
// New metrics after that should be added in order.
#[test]
#[rustfmt::skip] // Let's not add newlines unnecessary
fn correct_order() {
    use histogram::Histogram;
    use metrics::{Metric::*, TimeUnit};
    use std::time::Duration;
    use util::local_now_with_offset;

    // Extract the discriminant of the serialized value,
    // that is: the first 4 bytes.
    fn discriminant(metric: &metrics::Metric) -> u32 {
        let ser = bincode::serialize(metric).unwrap();
        (ser[0] as u32)
        | ((ser[1] as u32) << 8)
        | ((ser[2] as u32) << 16)
        | ((ser[3] as u32) << 24)
    }

    // One of every metric type. The values are arbitrary and don't matter.
    let long_string = "0123456789".repeat(200);
    let all_metrics = vec![
        Boolean(false),
        Counter(0),
        CustomDistributionExponential(Histogram::exponential(1, 500, 10)),
        CustomDistributionLinear(Histogram::linear(1, 500, 10)),
        Datetime(local_now_with_offset(), TimeUnit::Second),
        Experiment(RecordedExperiment { branch: "branch".into(), extra: None, }),
        Quantity(0),
        String("glean".into()),
        StringList(vec!["glean".into()]),
        Uuid("082c3e52-0a18-11ea-946f-0fe0c98c361c".into()),
        Timespan(Duration::new(5, 0), TimeUnit::Second),
        TimingDistribution(Histogram::functional(2.0, 8.0)),
        MemoryDistribution(Histogram::functional(2.0, 8.0)),
        Jwe("eyJhbGciOiJSU0EtT0FFUCIsImVuYyI6IkEyNTZHQ00ifQ.OKOawDo13gRp2ojaHV7LFpZcgV7T6DVZKTyKOMTYUmKoTCVJRgckCL9kiMT03JGeipsEdY3mx_etLbbWSrFr05kLzcSr4qKAq7YN7e9jwQRb23nfa6c9d-StnImGyFDbSv04uVuxIp5Zms1gNxKKK2Da14B8S4rzVRltdYwam_lDp5XnZAYpQdb76FdIKLaVmqgfwX7XWRxv2322i-vDxRfqNzo_tETKzpVLzfiwQyeyPGLBIO56YJ7eObdv0je81860ppamavo35UgoRdbYaBcoh9QcfylQr66oc6vFWXRcZ_ZT2LawVCWTIy3brGPi6UklfCpIMfIjf7iGdXKHzg.48V1_ALb6US04U3b.5eym8TW_c8SuK0ltJ3rpYIzOeDQz7TALvtu6UG9oMo4vpzs9tX_EFShS8iB7j6jiSdiwkIr3ajwQzaBtQD_A.XFBoMYUZodetZdvTiFvSkQ".into()),
        Rate(0, 0),
        Text(long_string),
        Object("{}".into()),
    ];

    for metric in all_metrics {
        let disc = discriminant(&metric);

        // DO NOT TOUCH THE EXPECTED VALUE.
        // If this test fails because of non-equal discriminants, that is a bug in the code, not
        // the test.

        // We're matching here, thus fail the build if new variants are added.
        match metric {
            Boolean(..)                       => assert_eq!( 0, disc),
            Counter(..)                       => assert_eq!( 1, disc),
            CustomDistributionExponential(..) => assert_eq!( 2, disc),
            CustomDistributionLinear(..)      => assert_eq!( 3, disc),
            Datetime(..)                      => assert_eq!( 4, disc),
            Experiment(..)                    => assert_eq!( 5, disc),
            Quantity(..)                      => assert_eq!( 6, disc),
            String(..)                        => assert_eq!( 7, disc),
            StringList(..)                    => assert_eq!( 8, disc),
            Uuid(..)                          => assert_eq!( 9, disc),
            Timespan(..)                      => assert_eq!(10, disc),
            TimingDistribution(..)            => assert_eq!(11, disc),
            MemoryDistribution(..)            => assert_eq!(12, disc),
            Jwe(..)                           => assert_eq!(13, disc),
            Rate(..)                          => assert_eq!(14, disc),
            Url(..)                           => assert_eq!(15, disc),
            Text(..)                          => assert_eq!(16, disc),
            Object(..)                        => assert_eq!(17, disc),
        }
    }
}

#[test]
#[rustfmt::skip] // Let's not merge lines
fn backwards_compatible_deserialization() {
    use std::env;
    use std::time::Duration;
    use chrono::prelude::*;
    use histogram::Histogram;
    use metrics::{Metric::*, TimeUnit};

    // Prepare some data to fill in
    let dt = FixedOffset::east(9*3600).ymd(2014, 11, 28).and_hms_nano(21, 45, 59, 12);

    let mut custom_dist_exp = Histogram::exponential(1, 500, 10);
    custom_dist_exp.accumulate(10);

    let mut custom_dist_linear = Histogram::linear(1, 500, 10);
    custom_dist_linear.accumulate(10);

    let mut time_dist = Histogram::functional(2.0, 8.0);
    time_dist.accumulate(10);

    let mut mem_dist = Histogram::functional(2.0, 16.0);
    mem_dist.accumulate(10);

    // One of every metric type. The values are arbitrary, but stable.
    let all_metrics = vec![
        (
            "boolean",
            vec![0, 0, 0, 0, 1],
            Boolean(true)
        ),
        (
            "counter",
            vec![1, 0, 0, 0, 20, 0, 0, 0],
            Counter(20)
        ),
        (
            "custom exponential distribution",
                vec![2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 1,
                     0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0,
                     0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 244, 1, 0, 0, 0, 0, 0, 0, 10, 0,
                     0, 0, 0, 0, 0, 0],
            CustomDistributionExponential(custom_dist_exp)
        ),
        (
            "custom linear distribution",
            vec![3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
                 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1, 0,
                 0, 0, 0, 0, 0, 0, 244, 1, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0],
            CustomDistributionLinear(custom_dist_linear)
        ),
        (
            "datetime",
            vec![4, 0, 0, 0, 35, 0, 0, 0, 0, 0, 0, 0, 50, 48, 49, 52, 45, 49, 49, 45,
                 50, 56, 84, 50, 49, 58, 52, 53, 58, 53, 57, 46, 48, 48, 48, 48, 48,
                 48, 48, 49, 50, 43, 48, 57, 58, 48, 48, 3, 0, 0, 0],
            Datetime(dt, TimeUnit::Second),
        ),
        (
            "experiment",
            vec![5, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 98, 114, 97, 110, 99, 104, 0],
            Experiment(RecordedExperiment { branch: "branch".into(), extra: None, }),
        ),
        (
            "quantity",
            vec![6, 0, 0, 0, 17, 0, 0, 0, 0, 0, 0, 0],
            Quantity(17)
        ),
        (
            "string",
            vec![7, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 103, 108, 101, 97, 110],
            String("glean".into())
        ),
        (
            "string list",
            vec![8, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,
                 103, 108, 101, 97, 110],
            StringList(vec!["glean".into()])
        ),
        (
            "uuid",
            vec![9, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0, 48, 56, 50, 99, 51, 101, 53, 50,
                 45, 48, 97, 49, 56, 45, 49, 49, 101, 97, 45, 57, 52, 54, 102, 45, 48,
                 102, 101, 48, 99, 57, 56, 99, 51, 54, 49, 99],
            Uuid("082c3e52-0a18-11ea-946f-0fe0c98c361c".into()),
        ),
        (
            "timespan",
            vec![10, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0],
            Timespan(Duration::new(5, 0), TimeUnit::Second),
        ),
        (
            "timing distribution",
            vec![11, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 123, 81, 125,
                 60, 184, 114, 241, 63],
            TimingDistribution(time_dist),
        ),
        (
            "memory distribution",
            vec![12, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 15, 137, 249,
                 108, 88, 181, 240, 63],
             MemoryDistribution(mem_dist),
        ),
    ];

    for (name, data, metric) in all_metrics {
        // Helper to print serialization data if instructed by environment variable
        // Run with:
        //
        // ```text
        // PRINT_DATA=1 cargo test -p glean-core --lib -- --nocapture backwards
        // ```
        //
        // This should not be necessary to re-run and change here, unless a bincode upgrade
        // requires us to also migrate existing data.
        if env::var("PRINT_DATA").is_ok() {
            let bindata = bincode::serialize(&metric).unwrap();
            println!("(\n    {:?},\n    vec!{:?},", name, bindata);
        } else {
            // Otherwise run the test
            let deserialized = bincode::deserialize(&data).unwrap();
            if let CustomDistributionExponential(hist) = &deserialized {
                hist.snapshot_values(); // Force initialization of the ranges
            }
            if let CustomDistributionLinear(hist) = &deserialized {
                hist.snapshot_values(); // Force initialization of the ranges
            }

            assert_eq!(
                metric, deserialized,
                "Expected properly deserialized {}",
                name
            );
        }
    }
}

#[test]
fn test_first_run() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    {
        let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
        // Check that this is indeed the first run.
        assert!(glean.is_first_run());
    }

    {
        // Other runs must be not marked as "first run".
        let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
        assert!(!glean.is_first_run());
    }
}

#[test]
fn test_dirty_bit() {
    let _ = env_logger::builder().try_init();
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    {
        let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
        // The dirty flag must not be set the first time Glean runs.
        assert!(!glean.is_dirty_flag_set());

        // Set the dirty flag and check that it gets correctly set.
        glean.set_dirty_flag(true);
        assert!(glean.is_dirty_flag_set());
    }

    {
        // Check that next time Glean runs, it correctly picks up the "dirty flag".
        // It is expected to be 'true'.
        let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
        assert!(glean.is_dirty_flag_set());

        // Set the dirty flag to false.
        glean.set_dirty_flag(false);
        assert!(!glean.is_dirty_flag_set());
    }

    {
        // Check that next time Glean runs, it correctly picks up the "dirty flag".
        // It is expected to be 'false'.
        let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
        assert!(!glean.is_dirty_flag_set());
    }
}

#[test]
fn test_change_metric_type_runtime() {
    let dir = tempfile::tempdir().unwrap();

    let (glean, _t) = new_glean(Some(dir));

    // We attempt to create two metrics: one with a 'string' type and the other
    // with a 'timespan' type, both being sent in the same pings and having the
    // same lifetime.
    let metric_name = "type_swap";
    let metric_category = "test";
    let metric_lifetime = Lifetime::Ping;
    let ping_name = "store1";

    let string_metric = StringMetric::new(CommonMetricData {
        name: metric_name.into(),
        category: metric_category.into(),
        send_in_pings: vec![ping_name.into()],
        disabled: false,
        lifetime: metric_lifetime,
        ..Default::default()
    });

    let string_value = "definitely-a-string!";
    string_metric.set_sync(&glean, string_value);

    assert_eq!(
        string_metric.get_value(&glean, ping_name).unwrap(),
        string_value,
        "Expected properly deserialized string"
    );

    let timespan_metric = TimespanMetric::new(
        CommonMetricData {
            name: metric_name.into(),
            category: metric_category.into(),
            send_in_pings: vec![ping_name.into()],
            disabled: false,
            lifetime: metric_lifetime,
            ..Default::default()
        },
        TimeUnit::Nanosecond,
    );

    let duration = 60;
    timespan_metric.set_start(&glean, 0);
    timespan_metric.set_stop(&glean, duration);

    assert_eq!(
        timespan_metric.get_value(&glean, ping_name).unwrap(),
        60,
        "Expected properly deserialized time"
    );

    // We expect old data to be lost forever. See the following bug comment
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1621757#c1 for more context.
    assert_eq!(None, string_metric.get_value(&glean, ping_name));
}

#[test]
fn timing_distribution_truncation() {
    let dir = tempfile::tempdir().unwrap();

    let (glean, _t) = new_glean(Some(dir));
    let max_sample_time = 1000 * 1000 * 1000 * 60 * 10;

    for (unit, expected_keys) in &[
        (
            TimeUnit::Nanosecond,
            HashSet::<i64>::from_iter(vec![961_548, 939, 599_512_966_122, 1]),
        ),
        (
            TimeUnit::Microsecond,
            HashSet::<i64>::from_iter(vec![939, 562_949_953_421_318, 599_512_966_122, 961_548]),
        ),
        (
            TimeUnit::Millisecond,
            HashSet::<i64>::from_iter(vec![
                961_548,
                576_460_752_303_431_040,
                599_512_966_122,
                562_949_953_421_318,
            ]),
        ),
    ] {
        let dist = TimingDistributionMetric::new(
            CommonMetricData {
                name: format!("local_metric_{:?}", unit),
                category: "local".into(),
                send_in_pings: vec!["baseline".into()],
                ..Default::default()
            },
            *unit,
        );

        for &value in &[
            1,
            1_000,
            1_000_000,
            max_sample_time,
            max_sample_time * 1_000,
            max_sample_time * 1_000_000,
        ] {
            let timer_id = 4u64.into();
            dist.set_start(timer_id, 0);
            dist.set_stop_and_accumulate(&glean, timer_id, value);
        }

        let snapshot = dist.get_value(&glean, "baseline").unwrap();

        let mut keys = HashSet::new();
        let mut recorded_values = 0;

        for (&key, &value) in &snapshot.values {
            // A snapshot potentially includes buckets with a 0 count.
            // We can ignore them here.
            if value > 0 {
                assert!((key as u64) < max_sample_time * unit.as_nanos(1));
                keys.insert(key);
                recorded_values += 1;
            }
        }

        assert_eq!(4, recorded_values);
        assert_eq!(keys, *expected_keys);

        // The number of samples was originally designed around 1ns to
        // 10minutes, with 8 steps per power of 2, which works out to 316 items.
        // This is to ensure that holds even when the time unit is changed.
        assert!(snapshot.values.len() < 316);
    }
}

#[test]
fn timing_distribution_truncation_accumulate() {
    let dir = tempfile::tempdir().unwrap();

    let (glean, _t) = new_glean(Some(dir));
    let max_sample_time = 1000 * 1000 * 1000 * 60 * 10;

    for &unit in &[
        TimeUnit::Nanosecond,
        TimeUnit::Microsecond,
        TimeUnit::Millisecond,
    ] {
        let dist = TimingDistributionMetric::new(
            CommonMetricData {
                name: format!("local_metric_{:?}", unit),
                category: "local".into(),
                send_in_pings: vec!["baseline".into()],
                ..Default::default()
            },
            unit,
        );

        let samples = [
            1,
            1000,
            100000,
            max_sample_time,
            max_sample_time * 1_000,
            max_sample_time * 1_000_000,
        ];
        let timer_id = 4u64.into(); // xkcd#221

        for sample in samples {
            dist.set_start(timer_id, 0);
            dist.set_stop_and_accumulate(&glean, timer_id, sample);
        }

        let snapshot = dist.get_value(&glean, "baseline").unwrap();

        // The number of samples was originally designed around 1ns to
        // 10minutes, with 8 steps per power of 2, which works out to 316 items.
        // This is to ensure that holds even when the time unit is changed.
        assert!(snapshot.values.len() < 316);
    }
}

#[test]
fn test_setting_debug_view_tag() {
    let dir = tempfile::tempdir().unwrap();

    let (mut glean, _t) = new_glean(Some(dir));

    let valid_tag = "valid-tag";
    assert!(glean.set_debug_view_tag(valid_tag));
    assert_eq!(valid_tag, glean.debug_view_tag().unwrap());

    let invalid_tag = "invalid tag";
    assert!(!glean.set_debug_view_tag(invalid_tag));
    assert_eq!(valid_tag, glean.debug_view_tag().unwrap());
}

#[test]
fn test_setting_log_pings() {
    let dir = tempfile::tempdir().unwrap();

    let (mut glean, _t) = new_glean(Some(dir));
    assert!(!glean.log_pings());

    glean.set_log_pings(true);
    assert!(glean.log_pings());

    glean.set_log_pings(false);
    assert!(!glean.log_pings());
}

#[test]
fn test_set_remote_metric_configuration() {
    let (glean, _t) = new_glean(None);
    let metric = StringMetric::new(CommonMetricData {
        category: "category".to_string(),
        name: "string_metric".to_string(),
        send_in_pings: vec!["baseline".to_string()],
        ..Default::default()
    });
    let another_metric = LabeledString::new(
        LabeledMetricData::Common {
            cmd: CommonMetricData {
                category: "category".to_string(),
                name: "labeled_string_metric".to_string(),
                send_in_pings: vec!["baseline".to_string()],
                ..Default::default()
            },
        },
        Some(vec!["label1".into()]),
    );

    // 1. Set the metrics with a "TEST_VALUE" and ensure it was set
    metric.set_sync(&glean, "TEST_VALUE");
    assert_eq!(
        "TEST_VALUE",
        metric.get_value(&glean, "baseline").unwrap(),
        "Initial value must match"
    );
    another_metric.get("label1").set_sync(&glean, "TEST_VALUE");
    assert_eq!(
        "TEST_VALUE",
        another_metric
            .get("label1")
            .get_value(&glean, "baseline")
            .unwrap(),
        "Initial value must match"
    );

    // 2. Set a configuration to disable the metrics
    let mut remote_settings_config = json!(
        {
            "metrics_enabled": {
                "category.string_metric": false,
                "category.labeled_string_metric": false,
            }
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 3. Since the metrics were disabled, setting a new value will be ignored
    metric.set_sync(&glean, "VALUE_AFTER_DISABLED");
    assert_eq!(
        "TEST_VALUE",
        metric.get_value(&glean, "baseline").unwrap(),
        "Shouldn't set when disabled"
    );
    another_metric
        .get("label1")
        .set_sync(&glean, "VALUE_AFTER_DISABLED");
    assert_eq!(
        "TEST_VALUE",
        another_metric
            .get("label1")
            .get_value(&glean, "baseline")
            .unwrap(),
        "Shouldn't set when disabled"
    );

    // 4. Set a new configuration where one metric is enabled
    remote_settings_config = json!(
        {
            "metrics_enabled": {
                "category.string_metric": true,
            }
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 5. Since the first metric is enabled, setting a new value should work
    // on it but not the second metric
    metric.set_sync(&glean, "VALUE_AFTER_REENABLED");
    assert_eq!(
        "VALUE_AFTER_REENABLED",
        metric.get_value(&glean, "baseline").unwrap(),
        "Should set when re-enabled"
    );
    another_metric
        .get("label1")
        .set_sync(&glean, "VALUE_AFTER_REENABLED");
    assert_eq!(
        "TEST_VALUE",
        another_metric
            .get("label1")
            .get_value(&glean, "baseline")
            .unwrap(),
        "Should not set if metric config entry unchanged"
    );

    // 6. Set a new configuration where the second metric is enabled. This
    // should be merged with the existing configuration and then both
    // metrics should be enabled at that point.
    remote_settings_config = json!(
        {
            "metrics_enabled": {
                "category.labeled_string_metric": true,
            }
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 7. Now both metrics are enabled, setting a new value should work for
    // both metrics with the merged configurations
    metric.set_sync(&glean, "FINAL VALUE");
    assert_eq!(
        "FINAL VALUE",
        metric.get_value(&glean, "baseline").unwrap(),
        "Should set when still enabled"
    );
    another_metric.get("label1").set_sync(&glean, "FINAL VALUE");
    assert_eq!(
        "FINAL VALUE",
        another_metric
            .get("label1")
            .get_value(&glean, "baseline")
            .unwrap(),
        "Should set when re-enabled"
    );
}

#[test]
fn test_remote_settings_epoch() {
    let (glean, _t) = new_glean(None);

    // 1. Ensure the starting epoch
    let mut current_epoch = glean.remote_settings_epoch.load(Ordering::Acquire);
    assert_eq!(0u8, current_epoch, "Current epoch must start at 0");

    // 2. Set a configuration which will trigger incrementing the epoch
    let remote_settings_config = json!(
        {
            "metrics_enabled": {
                "category.string_metric": false
            }
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 3. Ensure the epoch updated
    current_epoch = glean.remote_settings_epoch.load(Ordering::Acquire);
    assert_eq!(1u8, current_epoch, "Current epoch must match");
}

#[test]
fn test_remote_settings_epoch_updates_in_metric() {
    let (glean, _t) = new_glean(None);
    let metric = StringMetric::new(CommonMetricData {
        category: "category".to_string(),
        name: "string_metric".to_string(),
        send_in_pings: vec!["baseline".to_string()],
        ..Default::default()
    });

    // 1. Set the metric with a "TEST_VALUE" and ensure it was set
    metric.set_sync(&glean, "TEST_VALUE");
    assert_eq!(
        "TEST_VALUE",
        metric.get_value(&glean, "baseline").unwrap(),
        "Initial value must match"
    );

    // 2. Set a configuration to disable the `category.string_metric`
    let remote_settings_config = json!(
        {
            "metrics_enabled": {
                "category.string_metric": false
            }
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    // 3. Ensure the epoch was updated
    let current_epoch = glean.remote_settings_epoch.load(Ordering::Acquire);
    assert_eq!(1u8, current_epoch, "Current epoch must update");

    // 4. Since the metric was disabled, setting a new value will be ignored
    // AND the metric should update its epoch to match the `current_epoch`
    metric.set_sync(&glean, "VALUE_AFTER_DISABLED");
    assert_eq!(
        "TEST_VALUE",
        metric.get_value(&glean, "baseline").unwrap(),
        "Shouldn't set when disabled"
    );

    use crate::metrics::MetricType;
    // The "epoch" resides in the upper nibble of the `inner.disabled` field
    let epoch = metric.meta().disabled.load(Ordering::Acquire) >> 4;
    assert_eq!(
        current_epoch, epoch,
        "Epoch must match between metric and Glean core"
    );
}

#[test]
#[should_panic]
fn test_empty_application_id() {
    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();

    let glean = Glean::with_options(&tmpname, "", true, true);
    // Check that this is indeed the first run.
    assert!(glean.is_first_run());
}

#[test]
fn records_database_file_size() {
    let _ = env_logger::builder().is_test(true).try_init();

    // Note: We don't use `new_glean` because we need to re-use the database directory.

    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();

    // Initialize Glean once to ensure we create the database and did not error.
    let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);
    let database_size = &glean.database_metrics.size;
    let data = database_size.get_value(&glean, "metrics");

    assert!(data.is_none());

    drop(glean);

    // Initialize Glean again to record file size.
    let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, true);

    let database_size = &glean.database_metrics.size;
    let data = database_size.get_value(&glean, "metrics");
    assert!(data.is_some());
    let data = data.unwrap();

    // We should see the database containing some data.
    assert!(data.sum > 0);

    let rkv_load_state = &glean.database_metrics.rkv_load_error;
    let rkv_load_error = rkv_load_state.get_value(&glean, "metrics");

    assert_eq!(rkv_load_error, None);
}

#[cfg(not(target_os = "windows"))]
#[test]
fn records_io_errors() {
    use std::fs;
    let _ = env_logger::builder().is_test(true).try_init();

    let (glean, _data_dir) = new_glean(None);
    let pending_pings_dir = glean.get_data_path().join(crate::PENDING_PINGS_DIRECTORY);
    fs::create_dir_all(&pending_pings_dir).unwrap();
    let attr = fs::metadata(&pending_pings_dir).unwrap();
    let original_permissions = attr.permissions();

    // Remove write permissions on the pending_pings directory.
    let mut permissions = original_permissions.clone();
    permissions.set_readonly(true);
    fs::set_permissions(&pending_pings_dir, permissions).unwrap();

    // Writing the ping file should fail.
    let submitted = glean.internal_pings.baseline.submit_sync(&glean, None);
    // But the return value is still `true` because we enqueue the ping anyway.
    assert!(submitted);

    let metric = &glean.additional_metrics.io_errors;
    assert_eq!(
        1,
        metric.get_value(&glean, Some("metrics")).unwrap(),
        "Should have recorded an IO error"
    );

    // Restore write permissions.
    fs::set_permissions(&pending_pings_dir, original_permissions).unwrap();

    // Now we can submit a ping
    let submitted = glean.internal_pings.metrics.submit_sync(&glean, None);
    assert!(submitted);
}

#[test]
fn test_activity_api() {
    let _ = env_logger::builder().is_test(true).try_init();

    let dir = tempfile::tempdir().unwrap();
    let (mut glean, _t) = new_glean(Some(dir));

    // Signal that the client was active.
    glean.handle_client_active();

    // Check that we set everything we needed for the 'active' status.
    assert!(glean.is_dirty_flag_set());

    // Signal back that client is ianctive.
    glean.handle_client_inactive();

    // Check that we set everything we needed for the 'inactive' status.
    assert!(!glean.is_dirty_flag_set());
}

#[test]
fn disabled_pings_are_not_submitted() {
    let _ = env_logger::builder().is_test(true).try_init();

    let dir = tempfile::tempdir().unwrap();
    let (mut glean, _t) = new_glean(Some(dir));

    let ping = PingType::new_internal(
        "custom-disabled",
        true,
        false,
        true,
        true,
        false,
        vec![],
        vec![],
        true,
        vec![],
    );
    glean.register_ping_type(&ping);

    // We need to store a metric as an empty ping is not stored.
    let counter = CounterMetric::new(CommonMetricData {
        name: "counter".into(),
        category: "local".into(),
        send_in_pings: vec!["custom-disabled".into()],
        ..Default::default()
    });
    counter.add_sync(&glean, 1);

    assert!(!ping.submit_sync(&glean, None));
}

#[test]
fn internal_pings_can_be_disabled() {
    let _ = env_logger::builder().is_test(true).try_init();

    let dir = tempfile::tempdir().unwrap();
    let tmpname = dir.path().display().to_string();
    let glean = Glean::with_options(&tmpname, GLOBAL_APPLICATION_ID, true, false);

    // We need to store a metric as an empty ping is not stored.
    let counter = CounterMetric::new(CommonMetricData {
        name: "counter".into(),
        category: "local".into(),
        send_in_pings: vec!["baseline".into()],
        ..Default::default()
    });
    counter.add_sync(&glean, 1);

    let submitted = glean.internal_pings.baseline.submit_sync(&glean, None);
    assert!(!submitted);
}

#[test]
fn pings_are_controllable_from_remote_settings_config() {
    let _ = env_logger::builder().is_test(true).try_init();

    let dir = tempfile::tempdir().unwrap();
    let (mut glean, _t) = new_glean(Some(dir));

    let disabled_ping = PingType::new(
        "custom-disabled",
        true,
        true,
        true,
        true,
        false,
        vec![],
        vec![],
        true,
        vec![],
    );
    glean.register_ping_type(&disabled_ping);
    let enabled_ping = PingType::new(
        "custom-enabled",
        true,
        true,
        true,
        true,
        true,
        vec![],
        vec![],
        true,
        vec![],
    );
    glean.register_ping_type(&enabled_ping);

    assert!(!disabled_ping.submit_sync(&glean, None));
    assert!(enabled_ping.submit_sync(&glean, None));

    // Now, create a configuration to switch the enabled state of the two pings
    let remote_settings_config = json!(
        {
            "pings_enabled": {
                "custom-disabled": true,
                "custom-enabled": false
            }
        }
    )
    .to_string();
    glean
        .apply_server_knobs_config(RemoteSettingsConfig::try_from(remote_settings_config).unwrap());

    assert!(disabled_ping.submit_sync(&glean, None));
    assert!(!enabled_ping.submit_sync(&glean, None));
}
