/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Older tests that also use the DB.
// XXX: make them less complicated (since the transitions are covered in crate::tests::test_enrollment), just see if we write to the DB properly.

use std::str::FromStr;
use std::sync::{Arc, Mutex};

use uuid::Uuid;

use crate::enrollment::{
    DisqualifiedReason, EnrolledReason, EnrollmentChangeEvent, EnrollmentChangeEventType,
    EnrollmentStatus, EnrollmentsEvolver, ExperimentEnrollment, NotEnrolledReason,
};
use crate::stateful::behavior::EventStore;
use crate::stateful::enrollment::{
    get_enrolled_experiments, get_experiment_participation, get_rollout_participation,
    opt_in_with_branch, opt_out, reset_telemetry_identifiers, set_experiment_participation,
    set_rollout_participation,
};
use crate::stateful::persistence::{Database, Readable, StoreId};
use crate::tests::helpers::{TestMetrics, get_test_experiments, no_coenrolling_features};
use crate::{
    AppContext, AvailableRandomizationUnits, EnrolledExperiment, Experiment, NimbusTargetingHelper,
    Result,
};

fn get_experiment_enrollments<'r>(
    db: &Database,
    reader: &'r impl Readable<'r>,
) -> Result<Vec<ExperimentEnrollment>> {
    db.get_store(StoreId::Enrollments).collect_all(reader)
}

impl From<EventStore> for NimbusTargetingHelper {
    fn from(value: EventStore) -> Self {
        let ctx: AppContext = Default::default();
        NimbusTargetingHelper::new(ctx, Arc::new(Mutex::new(value)), None)
    }
}

fn get_experiments<'r>(
    db: &Database,
    reader: &'r impl Readable<'r>,
) -> Result<Vec<EnrolledExperiment>> {
    let enrollments: Vec<ExperimentEnrollment> =
        db.get_store(StoreId::Enrollments).collect_all(reader)?;
    let experiments: Vec<Experiment> = db.get_store(StoreId::Experiments).collect_all(reader)?;

    get_enrolled_experiments(&enrollments, &experiments)
}

#[test]
fn test_enrollments() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;
    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    let mut writer = db.write()?;
    let exp1 = get_test_experiments()[0].clone();
    let nimbus_id = Uuid::from_str("00000000-0000-0000-0000-000000000004")?;
    let aru = AvailableRandomizationUnits::with_nimbus_id(&nimbus_id);
    let mut targeting_attributes = AppContext {
        app_name: "fenix".to_string(),
        app_id: "org.mozilla.fenix".to_string(),
        channel: "nightly".to_string(),
        ..Default::default()
    }
    .into();
    assert_eq!(get_experiments(&db, &writer)?.len(), 0);

    let ids = no_coenrolling_features();
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut targeting_attributes, &ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &[exp1], None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 1);
    let enrollment = &enrollments[0];
    assert_eq!(enrollment.slug, "secure-gold");
    assert_eq!(enrollment.user_facing_name, "Diagnostic test experiment");
    assert_eq!(
        enrollment.user_facing_description,
        "This is a test experiment for diagnostic purposes."
    );
    assert!(enrollment.branch_slug == "control" || enrollment.branch_slug == "treatment");
    // Ensure the event was created too.
    assert_eq!(
        &events,
        &[EnrollmentChangeEvent {
            experiment_slug: "secure-gold".into(),
            branch_slug: "treatment".into(),
            reason: None,
            change: EnrollmentChangeEventType::Enrollment,
            feature_ids: vec!["some_control".into()],
        }]
    );
    // Get the ExperimentEnrollment from the DB.
    let ee: ExperimentEnrollment = db
        .get_store(StoreId::Enrollments)
        .get(&writer, "secure-gold")?
        .expect("should exist");
    assert!(matches!(
        ee.status,
        EnrollmentStatus::Enrolled {
            reason: EnrolledReason::Qualified,
            ..
        }
    ));

    // Now opt-out.
    let events = opt_out(&db, &mut writer, "secure-gold", None)?;
    assert_eq!(get_experiments(&db, &writer)?.len(), 0);
    // check we recorded the "why" correctly.
    let ee: ExperimentEnrollment = db
        .get_store(StoreId::Enrollments)
        .get(&writer, "secure-gold")?
        .expect("should exist");
    assert!(matches!(
        ee.status,
        EnrollmentStatus::Disqualified {
            reason: DisqualifiedReason::OptOut,
            ..
        }
    ));

    assert_eq!(
        &events,
        &[EnrollmentChangeEvent {
            experiment_slug: "secure-gold".into(),
            branch_slug: "treatment".into(),
            reason: Some("optout".into()),
            change: EnrollmentChangeEventType::Disqualification,
            feature_ids: vec!["some_control".into()],
        },]
    );

    // Opt in to a specific branch.
    let events = opt_in_with_branch(&db, &mut writer, "secure-gold", "treatment")?;
    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 1);
    let enrollment = &enrollments[0];
    assert_eq!(enrollment.slug, "secure-gold");
    assert_eq!(enrollment.branch_slug, "treatment");

    assert_eq!(
        &events,
        &[EnrollmentChangeEvent {
            experiment_slug: "secure-gold".into(),
            branch_slug: "treatment".into(),
            reason: None,
            change: EnrollmentChangeEventType::Enrollment,
            feature_ids: vec!["some_control".into()],
        },]
    );

    writer.commit()?;
    Ok(())
}

#[test]
fn test_updates() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;
    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    let mut writer = db.write()?;
    let nimbus_id = Uuid::from_str("00000000-0000-0000-0000-000000000004")?;
    let aru = AvailableRandomizationUnits::with_nimbus_id(&nimbus_id);
    let th = NimbusTargetingHelper::from(AppContext {
        app_name: "fenix".to_string(),
        app_id: "org.mozilla.fenix".to_string(),
        channel: "nightly".to_string(),
        ..Default::default()
    });
    assert_eq!(get_experiments(&db, &writer)?.len(), 0);
    let exps = get_test_experiments();

    let ids = no_coenrolling_features();
    let mut targeting_helper = th.clone();
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut targeting_helper, &ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &exps, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 2);

    assert_eq!(
        &events,
        &[
            EnrollmentChangeEvent {
                experiment_slug: "secure-gold".into(),
                branch_slug: "treatment".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["some_control".into()]
            },
            EnrollmentChangeEvent {
                experiment_slug: "secure-silver".into(),
                branch_slug: "treatment".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["about_welcome".into()]
            }
        ]
    );

    // pretend we just updated from the server and one of the 2 is missing.
    let exps = &[exps[1].clone()];
    let mut targeting_helper = th;
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut targeting_helper, &ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, exps, None)?;

    // should only have 1 now.
    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 1);
    // Check that the un-enrolled event was emitted.
    assert_eq!(
        &events,
        &[EnrollmentChangeEvent {
            experiment_slug: "secure-gold".into(),
            branch_slug: "treatment".into(),
            reason: None,
            change: EnrollmentChangeEventType::Unenrollment,
            feature_ids: vec!["some_control".into()]
        }]
    );

    writer.commit()?;
    Ok(())
}

#[test]
fn test_experiments_opt_out() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;
    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    let mut writer = db.write()?;
    let nimbus_id = Uuid::from_str("00000000-0000-0000-0000-000000000004")?;
    let th = NimbusTargetingHelper::from(AppContext {
        app_name: "fenix".to_string(),
        app_id: "org.mozilla.fenix".to_string(),
        channel: "nightly".to_string(),
        ..Default::default()
    });
    let aru = AvailableRandomizationUnits::with_nimbus_id(&nimbus_id);
    assert_eq!(get_experiments(&db, &writer)?.len(), 0);
    let exps = get_test_experiments();

    // User has opted out of new experiments.
    set_experiment_participation(&db, &mut writer, false)?;

    let ids = no_coenrolling_features();
    let mut targeting_helper = th.clone();
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut targeting_helper, &ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &exps, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 0);
    assert_eq!(&events, &[]);
    // We should see the experiment non-enrollments.
    assert_eq!(get_experiment_enrollments(&db, &writer)?.len(), 2);
    let num_not_enrolled_enrollments = get_experiment_enrollments(&db, &writer)?
        .into_iter()
        .filter(|enr| {
            matches!(
                enr.status,
                EnrollmentStatus::NotEnrolled {
                    reason: NotEnrolledReason::ExperimentsOptOut
                }
            )
        })
        .count();
    assert_eq!(num_not_enrolled_enrollments, 2);

    // User opts in, and updating should enroll us in 2 experiments.
    set_experiment_participation(&db, &mut writer, true)?;

    let mut targeting_helper = th.clone();
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut targeting_helper, &ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &exps, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 2);
    assert_eq!(
        &events,
        &[
            EnrollmentChangeEvent {
                experiment_slug: "secure-gold".into(),
                branch_slug: "treatment".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["some_control".into()]
            },
            EnrollmentChangeEvent {
                experiment_slug: "secure-silver".into(),
                branch_slug: "treatment".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["about_welcome".into()]
            }
        ]
    );
    // We should see 2 experiment enrollments.
    assert_eq!(get_experiment_enrollments(&db, &writer)?.len(), 2);
    let num_enrolled_enrollments = get_experiment_enrollments(&db, &writer)?
        .into_iter()
        .filter(|enr| matches!(enr.status, EnrollmentStatus::Enrolled { .. }))
        .count();
    assert_eq!(num_enrolled_enrollments, 2);

    // Opting out and updating should give us two disqualified enrollments
    set_experiment_participation(&db, &mut writer, false)?;

    let mut targeting_helper = th.clone();
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut targeting_helper, &ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &exps, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 0);
    assert_eq!(
        &events,
        &[
            EnrollmentChangeEvent {
                experiment_slug: "secure-gold".into(),
                branch_slug: "treatment".into(),
                reason: Some("experiments-opt-out".into()),
                change: EnrollmentChangeEventType::Disqualification,
                feature_ids: vec!["some_control".into()]
            },
            EnrollmentChangeEvent {
                experiment_slug: "secure-silver".into(),
                branch_slug: "treatment".into(),
                reason: Some("experiments-opt-out".into()),
                change: EnrollmentChangeEventType::Disqualification,
                feature_ids: vec!["about_welcome".into()]
            }
        ]
    );
    // We should see 2 experiment enrolments, this time they're both opt outs
    assert_eq!(get_experiment_enrollments(&db, &writer)?.len(), 2);

    assert_eq!(
        get_experiment_enrollments(&db, &writer)?
            .into_iter()
            .filter(|enr| {
                matches!(
                    enr.status,
                    EnrollmentStatus::Disqualified {
                        reason: DisqualifiedReason::ExperimentsOptOut,
                        ..
                    }
                )
            })
            .count(),
        2
    );

    // Opting in again and updating SHOULD NOT enroll us again (we've been disqualified).
    set_experiment_participation(&db, &mut writer, true)?;

    let mut targeting_helper = th;
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut targeting_helper, &ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &exps, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 0);
    assert_eq!(&events, &[]);

    assert_eq!(
        get_experiment_enrollments(&db, &writer)?
            .into_iter()
            .filter(|enr| {
                matches!(
                    enr.status,
                    EnrollmentStatus::Disqualified {
                        reason: DisqualifiedReason::ExperimentsOptOut,
                        ..
                    }
                )
            })
            .count(),
        2
    );

    writer.commit()?;
    Ok(())
}

#[test]
fn test_rollouts_opt_out() -> Result<()> {
    let tmp_dir = tempfile::tempdir()?;
    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    let mut writer = db.write()?;
    let nimbus_id = Uuid::from_str("00000000-0000-0000-0000-000000000004")?;
    let th = NimbusTargetingHelper::from(AppContext {
        app_name: "fenix".to_string(),
        app_id: "org.mozilla.fenix".to_string(),
        channel: "nightly".to_string(),
        ..Default::default()
    });
    let aru = AvailableRandomizationUnits::with_nimbus_id(&nimbus_id);
    assert_eq!(get_experiments(&db, &writer)?.len(), 0);
    let recipes = {
        let mut recipes = get_test_experiments();
        recipes[0].is_rollout = true;
        recipes[0].branches.remove(1);
        recipes[1].is_rollout = true;
        recipes[1].branches.remove(1);
        recipes
    };

    set_rollout_participation(&db, &mut writer, false)?;

    let coenrolling_feature_ids = no_coenrolling_features();

    let mut targeting_helper = th.clone();
    let mut evolver =
        EnrollmentsEvolver::new(&aru, &mut targeting_helper, &coenrolling_feature_ids);

    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &recipes, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(&enrollments, &[]);
    assert_eq!(&events, &[]);
    let experiment_enrollments = get_experiment_enrollments(&db, &writer)?;
    assert_eq!(experiment_enrollments.len(), 2);
    let num_not_enrolled_enrollments = experiment_enrollments
        .into_iter()
        .filter(|enr| {
            matches!(
                enr.status,
                EnrollmentStatus::NotEnrolled {
                    reason: NotEnrolledReason::RolloutsOptOut
                }
            )
        })
        .count();
    assert_eq!(num_not_enrolled_enrollments, 2);

    set_rollout_participation(&db, &mut writer, true)?;

    let mut targeting_helper = th.clone();
    let mut evolver =
        EnrollmentsEvolver::new(&aru, &mut targeting_helper, &coenrolling_feature_ids);

    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &recipes, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 2);
    assert_eq!(
        &events,
        &[
            EnrollmentChangeEvent {
                experiment_slug: "secure-gold".into(),
                branch_slug: "control".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["some_control".into()]
            },
            EnrollmentChangeEvent {
                experiment_slug: "secure-silver".into(),
                branch_slug: "control".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["about_welcome".into()]
            }
        ]
    );
    // We should see 2 experiment enrollments.
    assert_eq!(get_experiment_enrollments(&db, &writer)?.len(), 2);
    let num_enrolled_enrollments = get_experiment_enrollments(&db, &writer)?
        .into_iter()
        .filter(|enr| matches!(enr.status, EnrollmentStatus::Enrolled { .. }))
        .count();
    assert_eq!(num_enrolled_enrollments, 2);

    set_rollout_participation(&db, &mut writer, false)?;

    let mut targeting_helper = th.clone();
    let mut evolver =
        EnrollmentsEvolver::new(&aru, &mut targeting_helper, &coenrolling_feature_ids);
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &recipes, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 0);
    assert_eq!(
        &events,
        &[
            EnrollmentChangeEvent {
                experiment_slug: "secure-gold".into(),
                branch_slug: "control".into(),
                reason: Some("rollouts-opt-out".into()),
                change: EnrollmentChangeEventType::Disqualification,
                feature_ids: vec!["some_control".into()]
            },
            EnrollmentChangeEvent {
                experiment_slug: "secure-silver".into(),
                branch_slug: "control".into(),
                reason: Some("rollouts-opt-out".into()),
                change: EnrollmentChangeEventType::Disqualification,
                feature_ids: vec!["about_welcome".into()]
            }
        ]
    );
    // We should see 2 experiment enrolments, this time they're both opt outs
    assert_eq!(get_experiment_enrollments(&db, &writer)?.len(), 2);

    assert_eq!(
        get_experiment_enrollments(&db, &writer)?
            .into_iter()
            .filter(|enr| {
                matches!(
                    enr.status,
                    EnrollmentStatus::Disqualified {
                        reason: DisqualifiedReason::RolloutsOptOut,
                        ..
                    }
                )
            })
            .count(),
        2
    );

    // Opting in again and updating SHOULD re-enroll us again.
    set_rollout_participation(&db, &mut writer, true)?;

    let mut targeting_helper = th;
    let mut evolver =
        EnrollmentsEvolver::new(&aru, &mut targeting_helper, &coenrolling_feature_ids);
    println!("====");
    let events = evolver.evolve_enrollments_in_db(&db, &mut writer, &recipes, None)?;

    let enrollments = get_experiments(&db, &writer)?;
    assert_eq!(enrollments.len(), 2);
    assert_eq!(
        &events,
        &[
            EnrollmentChangeEvent {
                experiment_slug: "secure-gold".into(),
                branch_slug: "control".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["some_control".into()]
            },
            EnrollmentChangeEvent {
                experiment_slug: "secure-silver".into(),
                branch_slug: "control".into(),
                reason: None,
                change: EnrollmentChangeEventType::Enrollment,
                feature_ids: vec!["about_welcome".into()]
            }
        ]
    );

    assert_eq!(
        get_experiment_enrollments(&db, &writer)?
            .into_iter()
            .filter(|enr| { matches!(enr.status, EnrollmentStatus::Enrolled { .. }) })
            .count(),
        2
    );

    writer.commit()?;
    Ok(())
}

#[test]
fn test_telemetry_reset() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;
    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    let mut writer = db.write()?;

    let mock_exp1_slug = "exp-1".to_string();
    let mock_exp1_branch = "branch-1".to_string();
    let mock_exp2_slug = "exp-2".to_string();
    let mock_exp2_branch = "branch-2".to_string();
    let mock_exp3_slug = "exp-3".to_string();

    // Three currently-known experiments, in different states.
    let store = db.get_store(StoreId::Enrollments);
    store.put(
        &mut writer,
        &mock_exp1_slug,
        &ExperimentEnrollment {
            slug: mock_exp1_slug.clone(),
            status: EnrollmentStatus::new_enrolled(EnrolledReason::Qualified, &mock_exp1_branch),
        },
    )?;
    store.put(
        &mut writer,
        &mock_exp2_slug,
        &ExperimentEnrollment {
            slug: mock_exp2_slug.clone(),
            status: EnrollmentStatus::Disqualified {
                reason: DisqualifiedReason::Error,
                branch: mock_exp2_branch.clone(),
            },
        },
    )?;
    store.put(
        &mut writer,
        &mock_exp3_slug,
        &ExperimentEnrollment {
            slug: mock_exp3_slug.clone(),
            status: EnrollmentStatus::NotEnrolled {
                reason: NotEnrolledReason::NotTargeted,
            },
        },
    )?;
    writer.commit()?;

    let mut writer = db.write()?;
    let events = reset_telemetry_identifiers(&db, &mut writer)?;
    writer.commit()?;

    let enrollments = db.collect_all::<ExperimentEnrollment>(StoreId::Enrollments)?;
    assert_eq!(enrollments.len(), 3);

    // The enrolled experiment should have moved to disqualified.
    assert_eq!(enrollments[0].slug, mock_exp1_slug);
    assert!(
        matches!(&enrollments[0].status, EnrollmentStatus::Disqualified {
            reason: DisqualifiedReason::OptOut,
            branch,
            ..
        } if *branch == mock_exp1_branch)
    );

    // The disqualified experiment should have stayed disqualified.
    assert_eq!(enrollments[1].slug, mock_exp2_slug);
    assert!(
        matches!(&enrollments[1].status, EnrollmentStatus::Disqualified {
            reason: DisqualifiedReason::Error,
            branch,
            ..
        } if *branch == mock_exp2_branch)
    );

    // The not-enrolled experiment should have been unchanged.
    assert_eq!(enrollments[2].slug, mock_exp3_slug);
    assert!(matches!(
        &enrollments[2].status,
        EnrollmentStatus::NotEnrolled {
            reason: NotEnrolledReason::NotTargeted,
            ..
        }
    ));

    // We should have returned a single disqualification event.
    assert_eq!(events.len(), 1);
    assert!(matches!(&events[0], EnrollmentChangeEvent {
        change: EnrollmentChangeEventType::Disqualification,
        reason: Some(reason),
        experiment_slug,
        branch_slug,
        feature_ids,
    } if reason == "optout"
        && *experiment_slug == mock_exp1_slug
        && *branch_slug == mock_exp1_branch
        && feature_ids.is_empty()
    ));

    Ok(())
}

#[test]
fn test_experiments_opt_out_with_rollouts_opt_in() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;
    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    let mut writer = db.write()?;
    let nimbus_id = Uuid::from_str("00000000-0000-0000-0000-000000000004")?;
    let mut th = NimbusTargetingHelper::from(AppContext {
        app_name: "fenix".to_string(),
        app_id: "org.mozilla.fenix".to_string(),
        channel: "nightly".to_string(),
        ..Default::default()
    });
    let aru = AvailableRandomizationUnits::with_nimbus_id(&nimbus_id);

    // Create test experiment and rollout
    let mut experiment = get_test_experiments()[0].clone();
    experiment.slug = "test-experiment".to_string();
    experiment.is_rollout = false;

    let mut rollout = get_test_experiments()[0].clone();
    rollout.slug = "test-rollout".to_string();
    rollout.is_rollout = true;
    rollout.bucket_config.namespace = "test-rollout".to_string();

    // User opts out of experiments but stays opted in to rollouts
    set_experiment_participation(&db, &mut writer, false)?;
    set_rollout_participation(&db, &mut writer, true)?;

    // Verify flags are set correctly
    let exp_participation = get_experiment_participation(&db, &writer)?;
    let rollouts_participation = get_rollout_participation(&db, &writer)?;
    println!("Experiments participation: {}", exp_participation);
    println!("Rollouts participation: {}", rollouts_participation);

    let ids = no_coenrolling_features();
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut th, &ids);
    let events =
        evolver.evolve_enrollments_in_db(&db, &mut writer, &[experiment, rollout], None)?;

    let enrollments = get_experiment_enrollments(&db, &writer)?;
    println!("Total enrollments: {}", enrollments.len());
    for enrollment in &enrollments {
        println!(
            "Enrollment: slug={}, status={:?}",
            enrollment.slug, enrollment.status
        );
    }

    assert_eq!(
        &events,
        &[EnrollmentChangeEvent {
            experiment_slug: "test-rollout".into(),
            branch_slug: "control".into(),
            reason: None,
            change: EnrollmentChangeEventType::Enrollment,
            feature_ids: vec!["some_control".into()]
        }]
    );

    // Should be enrolled in rollout but not experiment
    let rollout_enrollment = enrollments.iter().find(|e| e.slug == "test-rollout");
    let experiment_enrollment = enrollments.iter().find(|e| e.slug == "test-experiment");

    assert!(
        rollout_enrollment.is_some(),
        "Rollout enrollment should exist"
    );
    assert!(matches!(
        rollout_enrollment.unwrap().status,
        EnrollmentStatus::Enrolled { .. }
    ));

    assert!(
        experiment_enrollment.is_some(),
        "Experiment enrollment should exist"
    );
    assert!(matches!(
        experiment_enrollment.unwrap().status,
        EnrollmentStatus::NotEnrolled {
            reason: NotEnrolledReason::ExperimentsOptOut
        }
    ));

    writer.commit()?;
    Ok(())
}

#[test]
fn test_rollouts_opt_out_with_experiments_opt_in() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;
    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    let mut writer = db.write()?;
    let nimbus_id = Uuid::from_str("00000000-0000-0000-0000-000000000004")?;
    let mut th = NimbusTargetingHelper::from(AppContext {
        app_name: "fenix".to_string(),
        app_id: "org.mozilla.fenix".to_string(),
        channel: "nightly".to_string(),
        ..Default::default()
    });
    let aru = AvailableRandomizationUnits::with_nimbus_id(&nimbus_id);

    // Create test experiment and rollout
    let mut experiment = get_test_experiments()[0].clone();
    experiment.slug = "test-experiment".to_string();
    experiment.is_rollout = false;

    let mut rollout = get_test_experiments()[0].clone();
    rollout.slug = "test-rollout".to_string();
    rollout.is_rollout = true;
    rollout.bucket_config.namespace = "test-rollout".to_string();

    // User opts out of rollouts but stays opted in to experiments
    set_experiment_participation(&db, &mut writer, true)?;
    set_rollout_participation(&db, &mut writer, false)?;

    // Verify flags are set correctly (using the same writer)
    let exp_participation = get_experiment_participation(&db, &writer)?;
    let rollouts_participation = get_rollout_participation(&db, &writer)?;
    println!("Experiments participation: {}", exp_participation);
    println!("Rollouts participation: {}", rollouts_participation);

    let ids = no_coenrolling_features();
    let mut evolver = EnrollmentsEvolver::new(&aru, &mut th, &ids);
    let events =
        evolver.evolve_enrollments_in_db(&db, &mut writer, &[experiment, rollout], None)?;

    // Use the same helper function as the working test
    let enrollments = get_experiment_enrollments(&db, &writer)?;
    println!("Total enrollments: {}", enrollments.len());
    for enrollment in &enrollments {
        println!(
            "Enrollment: slug={}, status={:?}",
            enrollment.slug, enrollment.status
        );
    }

    assert_eq!(
        &events,
        &[EnrollmentChangeEvent {
            experiment_slug: "test-experiment".into(),
            branch_slug: "control".into(),
            reason: None,
            change: EnrollmentChangeEventType::Enrollment,
            feature_ids: vec!["some_control".into()]
        }]
    );

    // Should be enrolled in experiment but not rollout
    let experiment_enrollment = enrollments.iter().find(|e| e.slug == "test-experiment");
    let rollout_enrollment = enrollments.iter().find(|e| e.slug == "test-rollout");

    assert!(
        experiment_enrollment.is_some(),
        "Experiment enrollment should exist"
    );
    assert!(matches!(
        experiment_enrollment.unwrap().status,
        EnrollmentStatus::Enrolled { .. }
    ));

    assert!(
        rollout_enrollment.is_some(),
        "Rollout enrollment should exist"
    );
    assert!(matches!(
        rollout_enrollment.unwrap().status,
        EnrollmentStatus::NotEnrolled {
            reason: NotEnrolledReason::RolloutsOptOut
        }
    ));

    writer.commit()?;
    Ok(())
}
