/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::collections::{HashMap, HashSet};
use std::sync::{Arc, RwLock};

use crate::enrollment::{
    EnrolledFeature, EnrolledFeatureConfig, ExperimentEnrollment, map_features_by_feature_id,
};
use crate::error::{NimbusError, Result, debug, warn};
use crate::evaluator::{CanEnrollResult, can_enroll};
use crate::stateful::enrollment::get_enrolled_experiments;
use crate::stateful::firefox_labs::FirefoxLabsMetadata;
use crate::stateful::gecko_prefs::GeckoPrefStore;
use crate::stateful::persistence::{Database, StoreId, Writer};
use crate::targeting::NimbusTargetingHelper;
use crate::{AvailableRandomizationUnits, EnrolledExperiment, Experiment};

// This module manages an in-memory cache of the database, so that some
// functions exposed by nimbus can return results without blocking on any
// IO. Consumers are expected to call our public `update()` function whenever
// the database might have changed.

// This struct is the cached data. This is never mutated, but instead
// recreated every time the cache is updated.
struct CachedData {
    pub experiments: Vec<Experiment>,
    pub enrollments: Vec<ExperimentEnrollment>,
    pub experiments_by_slug: HashMap<String, EnrolledExperiment>,
    pub features_by_feature_id: HashMap<String, EnrolledFeatureConfig>,
    pub gecko_pref_to_enrollment_slugs: Option<HashMap<String, HashSet<String>>>,
}

// This is the public cache API. Each NimbusClient can create one of these and
// it lives as long as the client - it encapsulates the synchronization needed
// to allow the cache to work correctly.
#[derive(Default)]
pub struct DatabaseCache {
    data: RwLock<Option<CachedData>>,
}

impl DatabaseCache {
    // Call this function whenever it's possible that anything cached by this
    // struct (eg, our enrollments) might have changed.
    //
    // This function must be passed a `&Database` and a `Writer`, which it
    // will commit before updating the in-memory cache. This is a slightly weird
    // API but it helps enforce two important properties:
    //
    //  * By requiring a `Writer`, we ensure mutual exclusion of other db writers
    //    and thus prevent the possibility of caching stale data.
    //  * By taking ownership of the `Writer`, we ensure that the calling code
    //    updates the cache after all of its writes have been performed.
    //  * `update_gecko_prefs` - Pass true for regular enrollment changes. Pass false
    //     when the Gecko prefs do not need to be synced with Gecko.
    pub fn commit_and_update(
        &self,
        db: &Database,
        writer: Writer,
        coenrolling_ids: &HashSet<&str>,
        gecko_pref_store: Option<Arc<GeckoPrefStore>>,
        update_gecko_prefs: bool,
    ) -> Result<()> {
        // By passing in the active `writer` we read the state of enrollments
        // as written by the calling code, before it's committed to the db.
        let enrollments: Vec<ExperimentEnrollment> =
            db.get_store(StoreId::Enrollments).collect_all(&writer)?;
        let experiments: Vec<Experiment> =
            db.get_store(StoreId::Experiments).collect_all(&writer)?;

        let enrolled_experiments = get_enrolled_experiments(&enrollments, &experiments)?;

        // Build a lookup table for experiments by experiment slug.
        // This will be used for get_experiment_branch() and get_active_experiments()
        let mut experiments_by_slug = HashMap::with_capacity(enrolled_experiments.len());
        for e in enrolled_experiments {
            experiments_by_slug.insert(e.slug.clone(), e);
        }

        let features_by_feature_id =
            map_features_by_feature_id(&enrollments, &experiments, coenrolling_ids);

        let gecko_pref_to_enrollment_slugs = gecko_pref_store.map(|store| {
            store.map_gecko_prefs_to_enrollment_slugs_and_update_store(
                &experiments,
                &enrollments,
                &experiments_by_slug,
                update_gecko_prefs,
            )
        });

        // This is where testing tools would override i.e. replace experimental feature configurations.
        // i.e. testing tools would cause custom feature configs to be stored in a Store.
        // Here, we get those overrides out of the store, and merge it with this map.

        // This is where rollouts (promoted experiments on a given feature) will be merged in to the feature variables.

        let data = CachedData {
            experiments,
            enrollments,
            experiments_by_slug,
            features_by_feature_id,
            gecko_pref_to_enrollment_slugs,
        };

        // Try to commit the change to disk and update the cache as close
        // together in time as possible. This leaves a small window where another
        // thread could read new data from disk but see old data in the cache,
        // but that seems benign in practice given the way we use the cache.
        // The alternative would be to lock the cache while we commit to disk,
        // and we don't want to risk blocking the main thread.
        writer.commit()?;
        let mut cached = self.data.write().unwrap();
        cached.replace(data);
        Ok(())
    }

    // Abstracts safely referencing our cached data.
    //
    // WARNING: because this manages locking, the callers of this need to be
    // careful regarding deadlocks - if the callback takes other own locks then
    // there's a risk of locks being taken in an inconsistent order. However,
    // there's nothing this code specifically can do about that.
    fn get_data<T, F>(&self, func: F) -> Result<T>
    where
        F: FnOnce(&CachedData) -> T,
    {
        match *self.data.read().unwrap() {
            None => {
                warn!("DatabaseCache attempting to read data before initialization is completed");
                Err(NimbusError::DatabaseNotReady)
            }
            Some(ref data) => Ok(func(data)),
        }
    }

    pub fn get_experiment_branch(&self, id: &str) -> Result<Option<String>> {
        self.get_data(|data| -> Option<String> {
            data.experiments_by_slug
                .get(id)
                .map(|experiment| experiment.branch_slug.clone())
        })
    }

    // This gives access to the feature JSON. We pass it as a string because uniffi doesn't
    // support JSON yet.
    pub fn get_feature_config_variables(&self, feature_id: &str) -> Result<Option<String>> {
        self.get_data(|data| {
            let enrolled_feature = data.features_by_feature_id.get(feature_id)?;
            let string = serde_json::to_string(&enrolled_feature.feature.value).unwrap();
            Some(string)
        })
    }

    pub fn get_enrollment_by_feature(&self, feature_id: &str) -> Result<Option<EnrolledFeature>> {
        self.get_data(|data| {
            data.features_by_feature_id
                .get(feature_id)
                .map(|feature| feature.into())
        })
    }

    pub fn get_active_experiments(&self) -> Result<Vec<EnrolledExperiment>> {
        self.get_data(|data| {
            data.experiments_by_slug
                .values()
                .map(|e| e.to_owned())
                .collect::<Vec<EnrolledExperiment>>()
        })
    }

    pub fn get_experiments(&self) -> Result<Vec<Experiment>> {
        self.get_data(|data| data.experiments.to_vec())
    }

    pub fn get_enrollments(&self) -> Result<Vec<ExperimentEnrollment>> {
        self.get_data(|data| data.enrollments.to_owned())
    }

    pub fn get_enrollments_for_pref(&self, pref: &str) -> Result<Option<HashSet<String>>> {
        self.get_data(|data| {
            if let Some(a) = &data.gecko_pref_to_enrollment_slugs {
                Ok(a.get(pref).cloned())
            } else {
                Ok(None)
            }
        })?
    }

    pub fn check_for_feature_conflict(
        &self,
        slug: &str,
        coenrolling_feature_ids: &[String],
    ) -> Result<Option<bool>> {
        self.get_data(|data| {
            if data.experiments_by_slug.contains_key(slug) {
                // Cannot conflict with itself.
                return Some(false);
            }

            if let Some(experiment) = data.experiments.iter().find(|e| e.slug == slug) {
                let coenrolling_feature_ids: HashSet<&str> =
                    coenrolling_feature_ids.iter().map(|s| s.as_ref()).collect();

                let enrolled_feature_ids =
                    compute_enrolled_feature_ids(&data.experiments_by_slug, true);

                Some(!features_available(
                    experiment,
                    &enrolled_feature_ids,
                    &coenrolling_feature_ids,
                ))
            } else {
                None
            }
        })
    }

    pub fn get_available_firefox_labs_metadata(
        &self,
        available_randomization_units: &AvailableRandomizationUnits,
        targeting_helper: &NimbusTargetingHelper,
        coenrolling_feature_ids: &[String],
    ) -> Result<Vec<FirefoxLabsMetadata>> {
        let mut all_labs: Vec<_> = self.get_data(|data| {
            let enrolled_feature_ids =
                compute_enrolled_feature_ids(&data.experiments_by_slug, true);

            let coenrolling_feature_ids: HashSet<&str> =
                coenrolling_feature_ids.iter().map(|s| s.as_ref()).collect();

            debug!("firefox labs: querying experiments...");
            let available = data
                .experiments
                .iter()
                .filter_map(|experiment| {
                    if !experiment.is_firefox_labs_opt_in {
                        debug!(
                            "firefox labs: {}: not a firefox labs opt-in",
                            experiment.slug
                        );
                        return None;
                    }

                    let enrolled = data.experiments_by_slug.contains_key(&experiment.slug);
                    match can_enroll(available_randomization_units, targeting_helper, experiment) {
                        CanEnrollResult::Enrollable { .. } => {}

                        CanEnrollResult::Unavailable { reason } => {
                            debug!("firefox labs: {}: unavailable: {}", experiment.slug, reason);
                            return None;
                        }

                        CanEnrollResult::TargetingError { reason } => {
                            debug!(
                                "firefox labs: {}: targeting error: {}",
                                experiment.slug, reason
                            );
                            return None;
                        }

                        CanEnrollResult::NotTargeted => {
                            debug!("firefox labs: {}: not targeted", experiment.slug);
                            return None;
                        }

                        CanEnrollResult::NotSelected => {
                            debug!("firefox labs: {}: not selected", experiment.slug);
                            return None;
                        }

                        CanEnrollResult::NoRandomizationUnit => {
                            debug!("firefox labs: {}: no randomization unit", experiment.slug);
                            return None;
                        }
                    }

                    if !enrolled {
                        let feature_conflict = !features_available(
                            experiment,
                            &enrolled_feature_ids,
                            &coenrolling_feature_ids,
                        );

                        if feature_conflict {
                            debug!("firefox labs: {}: feature conflict", experiment.slug);
                            return None;
                        }

                        if experiment.is_enrollment_paused {
                            debug!("firefox labs: {}: enrollment paused", experiment.slug);
                            return None;
                        }
                    }

                    let metadata = experiment.get_firefox_labs_metadata(enrolled);
                    if metadata.is_none() {
                        debug!("firefox labs: {}: invalid lab", experiment.slug);
                    } else {
                        debug!("firefox labs: {}: available", experiment.slug);
                    }

                    metadata
                })
                .collect();

            debug!("firefox labs: finished querying experiments");

            available
        })?;

        // XXX: This is maybe only useful for tests, but at least we get a
        // stable order.
        all_labs.sort_by(|e1, e2| Ord::cmp(&e1.slug, &e2.slug));

        Ok(all_labs)
    }

    #[cfg(test)]
    pub fn get_experiment_enrollment(&self, slug: &str) -> Result<Option<ExperimentEnrollment>> {
        self.get_data(|data| data.enrollments.iter().find(|e| e.slug == slug).cloned())
    }
}

fn compute_enrolled_feature_ids(
    experiments_by_slug: &HashMap<String, EnrolledExperiment>,
    is_rollout: bool,
) -> HashSet<&str> {
    experiments_by_slug
        .values()
        .filter(|e| e.is_rollout == is_rollout)
        .flat_map(|e| e.feature_ids.iter())
        .map(|f| f.as_ref())
        .collect()
}

fn features_available(
    experiment: &Experiment,
    enrolled_feature_ids: &HashSet<&str>,
    coenrolling_feature_ids: &HashSet<&str>,
) -> bool {
    for feature_id in &experiment.feature_ids {
        if enrolled_feature_ids.contains(&**feature_id)
            && !coenrolling_feature_ids.contains(&**feature_id)
        {
            return false;
        }
    }

    true
}
