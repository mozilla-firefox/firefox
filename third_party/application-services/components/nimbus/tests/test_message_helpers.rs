/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![cfg(feature = "stateful")]

// Testing get_experiment_branch semantics.

mod common;

use chrono::{Duration, Utc};
use nimbus::error::Result;
use serde_json::json;

#[test]
fn test_jexl_expression() -> Result<()> {
    let nimbus = crate::common::new_test_client("jexl_test")?;
    nimbus.initialize()?;

    nimbus.record_event("test".to_string(), 1)?;

    let helper = nimbus.create_targeting_helper(None)?;

    // We get a boolean back from a string!
    assert!(helper.eval_jexl("app_name == 'fenix'")?);

    // We get true and false back from two similar JEXL expressions!
    // I think we can convince ourselves that JEXL is being evaluated against the
    // AppContext.
    assert!(!helper.eval_jexl("app_name == 'xinef'")?);

    // The expression contains a variable not declared (snek_case Good, camelCase Bad)
    assert!(helper.eval_jexl("appName == 'fenix'").is_err());

    // This validates that helpers created from the create_targeting_helper have the event_store present in jexl operations
    assert!(helper.eval_jexl("'test'|eventSum('Days', 1, 0) == 1")?);

    let helper = nimbus.create_targeting_helper(
        json!(
        {
            "version": "95.1.0".to_string(),
        })
        .as_object()
        .cloned(),
    )?;

    // Check the versionCompare function, just to prove to ourselves that it's the same JEXL evaluator.
    assert!(helper.eval_jexl(
        "(version|versionCompare('95.!') >= 0) && (version|versionCompare('96.!') < 0)",
    )?);

    // Check the versionCompare function, just to prove to ourselves that it's the same JEXL evaluator.
    assert!(!helper.eval_jexl(
        "(version|versionCompare('96.!') >= 0) && (version|versionCompare('97.!') < 0)",
    )?);

    Ok(())
}

#[test]
fn test_derived_targeting_attributes_available() -> Result<()> {
    let nimbus = crate::common::new_test_client("jexl_test")?;
    nimbus.initialize()?;

    let helper = nimbus.create_targeting_helper(None)?;

    assert!(helper.eval_jexl("locale == 'en-GB'")?);

    assert!(helper.eval_jexl("language == 'en'")?);

    assert!(helper.eval_jexl("region == 'GB'")?);

    Ok(())
}

#[test]
fn test_derived_targeting_attributes_none() -> Result<()> {
    let mut nimbus = crate::common::new_test_client("jexl_test")?;
    nimbus.initialize()?;

    nimbus.with_targeting_attributes(Default::default());

    let helper = nimbus.create_targeting_helper(None)?;

    assert!(!helper.eval_jexl("(locale||'NONE') == 'en'")?);

    // assert!(helper.eval_jexl(
    //     "language == null".to_string()
    // )?);

    // assert!(helper.eval_jexl(
    //     "region == null".to_string()
    // )?);

    Ok(())
}
#[test]
fn test_jexl_expression_with_targeting_attributes() -> Result<()> {
    let mut nimbus = crate::common::new_test_client("jexl_test_days_since")?;
    nimbus.initialize()?;

    let helper = nimbus.create_targeting_helper(None)?;

    assert!(helper.eval_jexl("days_since_install == 0")?);

    assert!(helper.eval_jexl("days_since_update == 0")?);

    nimbus.set_install_time(Utc::now() - Duration::days(10));
    nimbus.set_update_time(Utc::now() - Duration::days(5));

    let helper = nimbus.create_targeting_helper(None)?;
    assert!(helper.eval_jexl("days_since_install == 10")?);

    assert!(helper.eval_jexl("days_since_update == 5")?);

    Ok(())
}

#[test]
fn test_string_helper() -> Result<()> {
    let nimbus = crate::common::new_test_client("string_helper_test")?;
    nimbus.initialize()?;

    let context = json!({
        "is_default_browser": true
    })
    .as_object()
    .cloned();
    let helper = nimbus.create_string_helper(context)?;
    let no_ctx_helper = nimbus.create_string_helper(None)?;

    let template = "{channel} {app_name} is {is_default_browser} {uuid}".to_string();
    assert_eq!(
        no_ctx_helper.string_format(template.clone(), None),
        "nightly fenix is {is_default_browser} {uuid}".to_string()
    );

    assert_eq!(
        helper.string_format(template.clone(), None),
        "nightly fenix is true {uuid}".to_string()
    );

    assert_eq!(
        helper.string_format(template.clone(), Some("EWE YOU EYE DEE".to_string())),
        "nightly fenix is true EWE YOU EYE DEE".to_string()
    );

    assert_eq!(
        no_ctx_helper.string_format(template, Some("EWE YOU EYE DEE".to_string())),
        "nightly fenix is {is_default_browser} EWE YOU EYE DEE".to_string()
    );

    // Test that UUID is generated only when uuid is in the template
    let template = "my {not-uuid}".to_string();
    let uuid = helper.get_uuid(template);
    assert!(uuid.is_none());

    let template = "my {uuid}".to_string();
    let uuid = helper.get_uuid(template.clone());
    assert!(uuid.is_some());
    assert_ne!(helper.string_format(template.clone(), uuid), template);

    Ok(())
}
