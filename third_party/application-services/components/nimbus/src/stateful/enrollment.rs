/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::iter;

use crate::enrollment::Participation;
use crate::enrollment::{
    DisqualifiedReason, EnrolledReason, EnrollmentChangeEvent, EnrollmentChangeEventType,
    EnrollmentsEvolver, ExperimentEnrollment, NotEnrolledReason, PreviousGeckoPrefState,
    map_enrollments,
};
use crate::error::{Result, debug, warn};
use crate::stateful::firefox_labs::{
    FirefoxLabsEnrollResult, FirefoxLabsEnrollStatus, FirefoxLabsUnenrollResult,
    FirefoxLabsUnenrollStatus,
};
use crate::stateful::gecko_prefs::GeckoPrefStore;
use crate::stateful::gecko_prefs::PrefUnenrollReason;
use crate::stateful::persistence::{
    DB_KEY_EXPERIMENT_PARTICIPATION, DB_KEY_ROLLOUT_PARTICIPATION,
    DEFAULT_EXPERIMENT_PARTICIPATION, DEFAULT_ROLLOUT_PARTICIPATION,
};
use crate::stateful::persistence::{Database, Readable, StoreId, Writer};
use crate::{EnrolledExperiment, EnrollmentStatus, Experiment};

impl EnrollmentsEvolver<'_> {
    /// Convenient wrapper around `evolve_enrollments` that fetches the current state of experiments,
    /// enrollments and user participation from the database.
    pub(crate) fn evolve_enrollments_in_db(
        &mut self,
        db: &Database,
        writer: &mut Writer,
        next_experiments: &[Experiment],
        gecko_pref_store: Option<&GeckoPrefStore>,
    ) -> Result<Vec<EnrollmentChangeEvent>> {
        // Get separate participation states from the db
        let is_participating_in_experiments = get_experiment_participation(db, writer)?;
        let is_participating_in_rollouts = get_rollout_participation(db, writer)?;

        let participation = Participation {
            in_experiments: is_participating_in_experiments,
            in_rollouts: is_participating_in_rollouts,
        };

        let experiments_store = db.get_store(StoreId::Experiments);
        let enrollments_store = db.get_store(StoreId::Enrollments);
        let prev_experiments: Vec<Experiment> = experiments_store.collect_all(writer)?;
        let prev_enrollments: Vec<ExperimentEnrollment> = enrollments_store.collect_all(writer)?;
        // Calculate the changes.
        let (next_enrollments, enrollments_change_events) = self.evolve_enrollments(
            participation,
            &prev_experiments,
            next_experiments,
            &prev_enrollments,
            gecko_pref_store,
        )?;
        let next_enrollments = map_enrollments(&next_enrollments);
        // Write the changes to the Database.
        enrollments_store.clear(writer)?;
        for enrollment in next_enrollments.values() {
            enrollments_store.put(writer, &enrollment.slug, *enrollment)?;
        }
        experiments_store.clear(writer)?;
        for experiment in next_experiments {
            // Sanity check.
            if !next_enrollments.contains_key(&experiment.slug) {
                error_support::report_error!(
                    "nimbus-evolve-enrollments",
                    "evolve_enrollments_in_db: experiment '{}' has no enrollment, dropping to keep database consistent",
                    &experiment.slug
                );
                continue;
            }
            experiments_store.put(writer, &experiment.slug, experiment)?;
        }
        Ok(enrollments_change_events)
    }
}

/// Return information about all enrolled experiments.
pub fn get_enrolled_experiments(
    enrollments: &[ExperimentEnrollment],
    experiments: &[Experiment],
) -> Result<Vec<EnrolledExperiment>> {
    let mut result = Vec::with_capacity(enrollments.len());
    for enrollment in enrollments {
        debug!("Have enrollment: {:?}", enrollment);
        if let EnrollmentStatus::Enrolled { branch, .. } = &enrollment.status {
            let experiment = experiments
                .iter()
                .find(|experiment| experiment.slug == enrollment.slug);

            match experiment {
                Some(experiment) => {
                    result.push(EnrolledExperiment {
                        feature_ids: experiment.get_feature_ids(),
                        slug: experiment.slug.clone(),
                        user_facing_name: experiment.user_facing_name.clone(),
                        user_facing_description: experiment.user_facing_description.clone(),
                        branch_slug: branch.to_string(),
                        is_rollout: experiment.is_rollout,
                    });
                }
                _ => {
                    warn!(
                        "Have enrollment {:?} but no matching experiment!",
                        enrollment
                    );
                }
            };
        }
    }
    Ok(result)
}

pub fn opt_in_with_branch(
    db: &Database,
    writer: &mut Writer,
    experiment_slug: &str,
    branch: &str,
) -> Result<Vec<EnrollmentChangeEvent>> {
    let mut events = vec![];
    if let Ok(Some(exp)) = db
        .get_store(StoreId::Experiments)
        .get::<Experiment, Writer>(writer, experiment_slug)
    {
        let enrollment = ExperimentEnrollment::from_explicit_opt_in(
            &exp,
            branch,
            EnrolledReason::OptIn,
            &mut events,
        );
        db.get_store(StoreId::Enrollments)
            .put(writer, experiment_slug, &enrollment.unwrap())?;
    } else {
        events.push(EnrollmentChangeEvent {
            experiment_slug: experiment_slug.to_string(),
            branch_slug: branch.to_string(),
            reason: Some("does-not-exist".to_string()),
            change: EnrollmentChangeEventType::EnrollFailed,
            feature_ids: vec![],
        });
    }

    Ok(events)
}

pub fn enroll_in_firefox_lab(
    db: &Database,
    writer: &mut Writer,
    slug: &str,
    feature_conflict: Option<bool>,
) -> Result<FirefoxLabsEnrollResult> {
    let mut events = vec![];

    let status = match feature_conflict {
        None => FirefoxLabsEnrollStatus::NoExperiment,

        Some(true) => FirefoxLabsEnrollStatus::FeatureConflict,

        Some(false) => match get_enrollment_and_experiment(db, writer, slug) {
            // We computed feature_conflict via the dbcache, so we actually
            // can't hit this case, but rewriting all the enrollment update in
            // terms of the dbcache is a much larger endeavour.
            //
            // This technically could have been written in terms of the dbcache
            // on the first pass, however, no other enrollment logic writes to
            // the database from the cache, so it would be less obvious if we
            // missed something.
            //
            // TODO(bug 2038055): rewrite in terms of the db cache
            Ok((_, None)) => FirefoxLabsEnrollStatus::NoExperiment,

            Ok((_, Some(experiment))) if !experiment.is_valid_firefox_lab() => {
                FirefoxLabsEnrollStatus::NotFirefoxLabsOptIn
            }

            Ok((Some(enrollment), _)) if enrollment.status.is_enrolled() => {
                FirefoxLabsEnrollStatus::AlreadyEnrolled
            }

            Ok((_, Some(experiment))) => {
                let new_enrollment = ExperimentEnrollment::from_explicit_opt_in(
                    &experiment,
                    &experiment.branches[0].slug,
                    EnrolledReason::FirefoxLabsOptIn,
                    &mut events,
                )?;
                db.get_store(StoreId::Enrollments)
                    .put(writer, slug, &new_enrollment)?;

                FirefoxLabsEnrollStatus::Enrolled
            }

            Err(_) => FirefoxLabsEnrollStatus::Error,
        },
    };

    if status != FirefoxLabsEnrollStatus::Enrolled {
        events.push(EnrollmentChangeEvent {
            experiment_slug: slug.to_string(),
            branch_slug: "N/A".to_string(),
            reason: Some(
                match status {
                    FirefoxLabsEnrollStatus::Enrolled => unreachable!("status != Enrolled"),
                    FirefoxLabsEnrollStatus::AlreadyEnrolled => "already-enrolled",
                    FirefoxLabsEnrollStatus::NoExperiment => "lab-does-not-exist",
                    FirefoxLabsEnrollStatus::NotFirefoxLabsOptIn => "not-lab",
                    FirefoxLabsEnrollStatus::FeatureConflict => "feature-conflict",
                    FirefoxLabsEnrollStatus::Error => "error",
                }
                .into(),
            ),
            change: EnrollmentChangeEventType::EnrollFailed,
            feature_ids: vec![],
        });
    }

    Ok(FirefoxLabsEnrollResult {
        status,
        enrollment_change_events: events,
    })
}

pub fn unenroll_from_firefox_lab(
    db: &Database,
    writer: &mut Writer,
    slug: &str,
    gecko_prefs: Option<&GeckoPrefStore>,
) -> Result<FirefoxLabsUnenrollResult> {
    let mut events = vec![];

    let status = match get_enrollment_and_experiment(db, writer, slug) {
        Ok((_, Some(experiment))) if !experiment.is_valid_firefox_lab() => {
            FirefoxLabsUnenrollStatus::NotFirefoxLabsOptIn
        }
        Ok((_, None)) => FirefoxLabsUnenrollStatus::NoExperiment,
        Ok((Some(enrollment), _)) if !enrollment.status.is_enrolled() => {
            FirefoxLabsUnenrollStatus::AlreadyUnenrolled
        }
        Ok((Some(enrollment), experiment)) => {
            let updated_enrollment = enrollment.on_explicit_opt_out(
                experiment.as_ref(),
                &mut events,
                DisqualifiedReason::FirefoxLabsOptOut,
                gecko_prefs,
            );
            db.get_store(StoreId::Enrollments)
                .put(writer, slug, &updated_enrollment)?;

            FirefoxLabsUnenrollStatus::Unenrolled
        }
        Ok((None, _)) => FirefoxLabsUnenrollStatus::NoExperiment,
        Err(_) => FirefoxLabsUnenrollStatus::Error,
    };

    if status != FirefoxLabsUnenrollStatus::Unenrolled {
        events.push(EnrollmentChangeEvent {
            experiment_slug: slug.into(),
            branch_slug: "N/A".into(),
            reason: Some(
                match status {
                    FirefoxLabsUnenrollStatus::Unenrolled => unreachable!("status != Unenrolled"),
                    FirefoxLabsUnenrollStatus::AlreadyUnenrolled => "already-unenrolled",
                    FirefoxLabsUnenrollStatus::NoExperiment => "lab-does-not-exist",
                    FirefoxLabsUnenrollStatus::NotFirefoxLabsOptIn => "not-lab",
                    FirefoxLabsUnenrollStatus::Error => "error",
                }
                .into(),
            ),
            change: EnrollmentChangeEventType::UnenrollFailed,
            feature_ids: vec![],
        });
    }

    Ok(FirefoxLabsUnenrollResult {
        status,
        enrollment_change_events: events,
    })
}

pub fn unenroll_from_all_firefox_labs(
    db: &Database,
    writer: &mut Writer,
    gecko_prefs: Option<&GeckoPrefStore>,
) -> Result<Vec<EnrollmentChangeEvent>> {
    // TODO(bug 2038055): Compute this using the database cache.

    let mut events = vec![];
    let enrollments: Vec<ExperimentEnrollment> =
        db.get_store(StoreId::Enrollments).collect_all(writer)?;

    for enrollment in &enrollments {
        if !enrollment
            .status
            .is_enrolled_with_reason(EnrolledReason::FirefoxLabsOptIn)
        {
            continue;
        }
        let experiment: Option<Experiment> = db
            .get_store(StoreId::Experiments)
            .get(writer, &enrollment.slug)?;

        let updated_enrollment = enrollment.on_explicit_opt_out(
            experiment.as_ref(),
            &mut events,
            DisqualifiedReason::FirefoxLabsOptOut,
            gecko_prefs,
        );

        db.get_store(StoreId::Enrollments)
            .put(writer, &enrollment.slug, &updated_enrollment)?;
    }

    Ok(events)
}

fn get_enrollment_and_experiment(
    db: &Database,
    writer: &mut Writer,
    experiment_slug: &str,
) -> Result<(Option<ExperimentEnrollment>, Option<Experiment>)> {
    // TODO(bug 2038055): Compute this using the database cache.
    let maybe_enrollment: Option<ExperimentEnrollment> = db
        .get_store(StoreId::Enrollments)
        .get(writer, experiment_slug)?;
    let maybe_experiment: Option<Experiment> = db
        .get_store(StoreId::Experiments)
        .get(writer, experiment_slug)?;

    // We are technically guaranteed at this time that if an active enrollment
    // exists in the enrollments store that the corresponding experiment must
    // also exist in the experiment store.
    //
    // This is only not true during apply_pending_experiments.
    Ok((maybe_enrollment, maybe_experiment))
}

pub fn opt_out(
    db: &Database,
    writer: &mut Writer,
    experiment_slug: &str,
    gecko_prefs: Option<&GeckoPrefStore>,
) -> Result<Vec<EnrollmentChangeEvent>> {
    let mut events = vec![];

    match get_enrollment_and_experiment(db, writer, experiment_slug) {
        Ok((Some(existing_enrollment), maybe_experiment)) => {
            let updated_enrollment = &existing_enrollment.on_explicit_opt_out(
                maybe_experiment.as_ref(),
                &mut events,
                DisqualifiedReason::OptOut,
                gecko_prefs,
            );

            db.get_store(StoreId::Enrollments)
                .put(writer, experiment_slug, updated_enrollment)?;
        }

        _ => {
            events.push(EnrollmentChangeEvent {
                experiment_slug: experiment_slug.to_string(),
                branch_slug: "N/A".to_string(),
                reason: Some("does-not-exist".to_string()),
                change: EnrollmentChangeEventType::UnenrollFailed,
                feature_ids: vec![],
            });
        }
    }

    Ok(events)
}

#[cfg(feature = "stateful")]
pub fn unenroll_for_pref(
    db: &Database,
    writer: &mut Writer,
    experiment_slug: &str,
    unenroll_reason: PrefUnenrollReason,
    triggering_pref_name: &str,
    gecko_pref_store: Option<&GeckoPrefStore>,
    events: &mut Vec<EnrollmentChangeEvent>,
) -> Result<()> {
    match get_enrollment_and_experiment(db, writer, experiment_slug) {
        Ok((Some(existing_enrollment), maybe_experiment)) => {
            existing_enrollment
                .maybe_revert_unchanged_gecko_pref_states(triggering_pref_name, gecko_pref_store);

            let updated_enrollment = &existing_enrollment.on_pref_unenroll(
                unenroll_reason,
                maybe_experiment.as_ref(),
                events,
            );
            db.get_store(StoreId::Enrollments)
                .put(writer, experiment_slug, updated_enrollment)?;
        }

        _ => {
            events.push(EnrollmentChangeEvent {
                experiment_slug: experiment_slug.to_string(),
                branch_slug: "N/A".to_string(),
                reason: Some("does-not-exist".to_string()),
                change: EnrollmentChangeEventType::UnenrollFailed,
                feature_ids: vec![],
            });
        }
    }

    Ok(())
}

pub fn get_experiment_participation<'r>(
    db: &Database,
    reader: &'r impl Readable<'r>,
) -> Result<bool> {
    let store = db.get_store(StoreId::Meta);
    let opted_in = store.get::<bool, _>(reader, DB_KEY_EXPERIMENT_PARTICIPATION)?;
    if let Some(opted_in) = opted_in {
        Ok(opted_in)
    } else {
        Ok(DEFAULT_EXPERIMENT_PARTICIPATION)
    }
}

pub fn get_rollout_participation<'r>(db: &Database, reader: &'r impl Readable<'r>) -> Result<bool> {
    let store = db.get_store(StoreId::Meta);
    let opted_in = store.get::<bool, _>(reader, DB_KEY_ROLLOUT_PARTICIPATION)?;
    if let Some(opted_in) = opted_in {
        Ok(opted_in)
    } else {
        Ok(DEFAULT_ROLLOUT_PARTICIPATION)
    }
}

pub fn set_experiment_participation(
    db: &Database,
    writer: &mut Writer,
    opt_in: bool,
) -> Result<bool> {
    let old_value =
        get_experiment_participation(db, writer).unwrap_or(DEFAULT_EXPERIMENT_PARTICIPATION);

    let store = db.get_store(StoreId::Meta);
    store.put(writer, DB_KEY_EXPERIMENT_PARTICIPATION, &opt_in)?;

    Ok(old_value)
}

pub fn set_rollout_participation(db: &Database, writer: &mut Writer, opt_in: bool) -> Result<bool> {
    let old_value = get_rollout_participation(db, writer).unwrap_or(DEFAULT_ROLLOUT_PARTICIPATION);
    let store = db.get_store(StoreId::Meta);
    store.put(writer, DB_KEY_ROLLOUT_PARTICIPATION, &opt_in)?;

    Ok(old_value)
}

/// Reset unique identifiers in response to application-level telemetry reset.
///
pub fn reset_telemetry_identifiers(
    db: &Database,
    writer: &mut Writer,
) -> Result<Vec<EnrollmentChangeEvent>> {
    let mut events = vec![];
    let store = db.get_store(StoreId::Enrollments);
    let enrollments: Vec<ExperimentEnrollment> = store.collect_all(writer)?;
    // TODO(bug 2038055): Compute this using the database cache.
    let experiments: Vec<Option<Experiment>> = enrollments
        .iter()
        .map(|enrollment| {
            db.get_store(StoreId::Experiments)
                .get::<Experiment, _>(writer, &enrollment.slug)
        })
        .collect::<Result<_>>()?;

    let updated_enrollments =
        iter::zip(enrollments, experiments).map(|(enrollment, experiment)| {
            enrollment.reset_telemetry_identifiers(experiment.as_ref(), &mut events)
        });
    store.clear(writer)?;
    for enrollment in updated_enrollments {
        store.put(writer, &enrollment.slug, &enrollment)?;
    }
    Ok(events)
}

pub mod v3 {
    // This module contains legacy enrollment structs that mirror the schema of enrollments stored as they were in the database as of v3. These are used for deserializing pre-migration enrollments during the migration process, and should not be used outside of that context.

    use super::*;
    use serde::{Deserialize, Serialize};

    #[derive(Deserialize, Serialize, Debug, Clone, Hash, Eq, PartialEq)]
    pub enum LegacyNotEnrolledReason {
        DifferentAppName,
        DifferentChannel,
        EnrollmentsPaused,
        FeatureConflict,
        NotSelected,
        NotTargeted,
        OptOut,
    }

    #[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
    pub struct LegacyExperimentEnrollment {
        pub slug: String,
        pub status: LegacyEnrollmentStatus,
    }

    #[derive(Deserialize, Serialize, Debug, Clone, Hash, Eq, PartialEq)]
    pub enum LegacyEnrollmentStatus {
        Enrolled {
            reason: EnrolledReason,
            branch: String,
            #[serde(skip_serializing_if = "Option::is_none")]
            prev_gecko_pref_states: Option<Vec<PreviousGeckoPrefState>>,
        },
        NotEnrolled {
            reason: LegacyNotEnrolledReason,
        },
        Disqualified {
            reason: DisqualifiedReason,
            branch: String,
        },
        WasEnrolled {
            branch: String,
            experiment_ended_at: u64,
        },
        Error {
            reason: String,
        },
    }

    impl From<LegacyNotEnrolledReason> for NotEnrolledReason {
        #[allow(deprecated)]
        fn from(value: LegacyNotEnrolledReason) -> Self {
            match value {
                LegacyNotEnrolledReason::DifferentAppName => NotEnrolledReason::DifferentAppName,
                LegacyNotEnrolledReason::DifferentChannel => NotEnrolledReason::DifferentChannel,
                LegacyNotEnrolledReason::EnrollmentsPaused => NotEnrolledReason::EnrollmentsPaused,
                LegacyNotEnrolledReason::FeatureConflict => NotEnrolledReason::FeatureConflict {
                    conflict_slug: None,
                },
                LegacyNotEnrolledReason::NotSelected => NotEnrolledReason::NotSelected,
                LegacyNotEnrolledReason::NotTargeted => NotEnrolledReason::NotTargeted,
                LegacyNotEnrolledReason::OptOut => NotEnrolledReason::OptOut,
            }
        }
    }

    impl From<LegacyEnrollmentStatus> for EnrollmentStatus {
        fn from(value: LegacyEnrollmentStatus) -> Self {
            match value {
                LegacyEnrollmentStatus::Enrolled {
                    reason,
                    branch,
                    prev_gecko_pref_states,
                } => EnrollmentStatus::Enrolled {
                    reason,
                    branch,
                    prev_gecko_pref_states,
                },
                LegacyEnrollmentStatus::NotEnrolled { reason } => EnrollmentStatus::NotEnrolled {
                    reason: reason.into(),
                },
                LegacyEnrollmentStatus::Disqualified { reason, branch } => {
                    EnrollmentStatus::Disqualified { reason, branch }
                }
                LegacyEnrollmentStatus::WasEnrolled {
                    branch,
                    experiment_ended_at,
                } => EnrollmentStatus::WasEnrolled {
                    branch,
                    experiment_ended_at,
                },
                LegacyEnrollmentStatus::Error { reason } => EnrollmentStatus::Error { reason },
            }
        }
    }

    impl From<LegacyExperimentEnrollment> for ExperimentEnrollment {
        fn from(value: LegacyExperimentEnrollment) -> Self {
            ExperimentEnrollment {
                slug: value.slug,
                status: value.status.into(),
            }
        }
    }
}
