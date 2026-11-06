/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![cfg(feature = "stateful")]

// Testing get_experiment_branch semantics.

mod common;

use nimbus::error::{NimbusError, Result};

#[test]
fn test_before_open() -> Result<()> {
    error_support::init_for_tests();
    let client = crate::common::new_test_client("test_before_open")?;
    assert!(matches!(
        client.get_experiment_branch("foo".to_string()),
        Err(NimbusError::DatabaseNotReady)
    ));
    // now initialize the DB - it should start working (and report no branch)
    client.initialize()?;
    assert_eq!(client.get_experiment_branch("foo".to_string())?, None);
    Ok(())
}

#[test]
fn test_enrolled() -> Result<()> {
    error_support::init_for_tests();
    let client = crate::common::new_test_client("test_enrolled")?;
    client.initialize()?;
    client.set_experiments_locally(crate::common::experiments_testing_feature_ids())?;

    let experiment_slugs = vec!["secure-gold", "no-features"];
    for experiment_slug in &experiment_slugs {
        // haven't applied them yet, so not enrolled as the experiment doesn't
        // really exist.
        assert_eq!(
            client.get_experiment_branch(experiment_slug.to_string())?,
            None,
            "shouldn't return anything before pending experiments applied"
        );
    }

    client.apply_pending_experiments()?;
    for experiment_slug in &experiment_slugs {
        // these experiements enroll everyone - not clear what treatment though.
        assert!(
            client
                .get_experiment_branch(experiment_slug.to_string())?
                .is_some(),
            "{} should return a branch for an experiment that enrolls everyone",
            experiment_slug
        );

        client.opt_out(experiment_slug.to_string())?;
        assert_eq!(
            client.get_experiment_branch(experiment_slug.to_string())?,
            None,
            "{} should not return a branch we've just opted out of",
            experiment_slug
        );

        client.opt_in_with_branch(experiment_slug.to_string(), "treatment".to_string())?;
        assert_eq!(
            client.get_experiment_branch(experiment_slug.to_string())?,
            Some("treatment".to_string()),
            "{} should return a treatment branch we've just opted into",
            experiment_slug
        );

        client.opt_in_with_branch(experiment_slug.to_string(), "control".to_string())?;
        assert_eq!(
            client.get_experiment_branch(experiment_slug.to_string())?,
            Some("control".to_string()),
            "{} should return a second branch we've just opted into",
            experiment_slug
        );
    }
    client.set_experiment_participation(false)?;
    for experiment_slug in experiment_slugs {
        assert_eq!(
            client.get_experiment_branch(experiment_slug.to_string())?,
            None,
            "{} should not return a branch if we've globally opted out",
            experiment_slug
        );
    }

    Ok(())
}
