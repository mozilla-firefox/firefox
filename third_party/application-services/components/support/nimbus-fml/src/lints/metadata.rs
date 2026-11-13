/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Lints about a feature's metadata: who owns it, where it's documented, and where
//! to file bugs against it.

use super::{feature_path, LintInfo, Linter, RawFinding};
use crate::intermediate_representation::{FeatureDef, FeatureManifest};

define_lints! {
    MISSING_META_BUG: Metadata, Warning =
        "Features should say where bugs against them are filed.",
        "Add a `meta-bug` URL, so that QA and experiment owners know where to file issues with the feature.";
    MISSING_DOCUMENTATION: Metadata, Warning =
        "Features should link to at least one document describing them.",
        "Add a `documentation` list of named URLs, e.g. user docs, QA docs or the feature's design document.";
    MISSING_CONTACTS: Metadata, Warning =
        "Features should name at least one person to ask about them.",
        "Add a `contacts` list of one or more email addresses (with Mozilla Jira accounts), so that questions about the feature reach someone who can answer them.";
    INVALID_CONTACT: Metadata, Warning =
        "Contacts should be email addresses.",
        "Contacts are used to route QA questions, so they need to be addresses that can be written to.";
}

pub(crate) struct Metadata;

impl Linter for Metadata {
    fn lints(&self) -> &'static [&'static LintInfo] {
        LINTS
    }

    fn check_feature(
        &self,
        feature: &FeatureDef,
        _manifest: &FeatureManifest,
        out: &mut Vec<RawFinding>,
    ) {
        check_feature(feature, out);
    }
}

fn check_feature(feature: &FeatureDef, out: &mut Vec<RawFinding>) {
    let metadata = &feature.metadata;
    let path = feature_path(feature);

    if metadata.meta_bug.is_none() {
        out.push(RawFinding::new(
            &MISSING_META_BUG,
            path.clone(),
            "No `meta-bug`",
        ));
    }

    if metadata.documentation.is_empty() {
        out.push(RawFinding::new(
            &MISSING_DOCUMENTATION,
            path.clone(),
            "No `documentation`",
        ));
    }

    if metadata.contacts.is_empty() {
        out.push(RawFinding::new(
            &MISSING_CONTACTS,
            path.clone(),
            "No `contacts`",
        ));
    }

    for contact in &metadata.contacts {
        if !is_email_address(contact) {
            out.push(RawFinding::new(
                &INVALID_CONTACT,
                path.clone(),
                format!("`{contact}` doesn't look like an email address"),
            ));
        }
    }
}

/// Filters out team names and handles; it doesn't validate addresses.
fn is_email_address(contact: &str) -> bool {
    if contact.trim() != contact || contact.chars().any(char::is_whitespace) {
        return false;
    }
    let mut parts = contact.split('@');
    match (parts.next(), parts.next(), parts.next()) {
        (Some(local), Some(domain), None) => {
            !local.is_empty()
                && domain.contains('.')
                && !domain.starts_with('.')
                && !domain.ends_with('.')
        }
        _ => false,
    }
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    use crate::frontend::DocumentationLink;
    use std::str::FromStr;
    use url::Url;

    fn feature() -> FeatureDef {
        FeatureDef::new("my-feature", "A description", Default::default(), false)
    }

    fn lints(feature: &FeatureDef) -> Vec<&'static str> {
        let mut out = Vec::new();
        check_feature(feature, &mut out);
        out.iter().map(|f| f.lint.name).collect()
    }

    #[test]
    fn test_empty_metadata() {
        assert_eq!(
            lints(&feature()),
            vec![
                "MISSING_META_BUG",
                "MISSING_DOCUMENTATION",
                "MISSING_CONTACTS"
            ]
        );
    }

    #[test]
    fn test_complete_metadata() -> crate::error::Result<()> {
        let mut feature = feature();
        feature.metadata.meta_bug = Some(Url::from_str("https://example.com/EXP-23")?);
        feature.metadata.contacts = vec!["jdoe@example.com".to_string()];
        feature.metadata.documentation = vec![DocumentationLink {
            name: "User documentation".to_string(),
            url: Url::from_str("https://example.info/my-feature")?,
        }];

        assert!(lints(&feature).is_empty());
        Ok(())
    }

    #[test]
    fn test_contacts_that_arent_addresses() {
        let mut feature = feature();
        feature.metadata.contacts = vec!["the nimbus team".to_string()];

        assert!(lints(&feature).contains(&"INVALID_CONTACT"));
    }

    #[test]
    fn test_is_email_address() {
        for ok in ["jdoe@example.com", "j.doe+nimbus@example.co.uk"] {
            assert!(is_email_address(ok), "{ok} should be an address");
        }
        for not_ok in [
            "jdoe",
            "the nimbus team",
            "jdoe@example",
            "@example.com",
            "jdoe@@example.com",
            " jdoe@example.com",
        ] {
            assert!(
                !is_email_address(not_ok),
                "{not_ok} shouldn't be an address"
            );
        }
    }
}
