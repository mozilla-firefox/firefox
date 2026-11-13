// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Lints for Nimbus feature manifests.
//!
//! These lints attempt to guide developers towards writing good, maintainable
//! features.
//!
//! A lint can be silenced by a `no-lint` annotation on a feature.

mod design;
mod documentation;
mod macros;
mod metadata;
mod naming;

use std::collections::HashSet;

use serde::Serialize;

use crate::intermediate_representation::{
    EnumDef, FeatureDef, FeatureManifest, ObjectDef, PropDef, TypeRef,
};
pub(crate) use crate::lints::macros::*;

define_lints! {
    UNKNOWN_LINT: Lints =
        "A `no-lint` entry names a lint that doesn't exist.",
        "Run `nimbus-fml lint --list` to see the available lints.";
}

/// A linter for a specific category.
///
/// By default each method does not report any issues. Implementors only need to
/// implement relevent methods.
pub(crate) trait Linter: Sync {
    /// The `LINTS` slice [`define_lints!`] generated for the module.
    fn lints(&self) -> &'static [&'static LintInfo];

    /// Lint a feature.
    fn lint_feature(
        &self,
        _feature: &FeatureDef,
        _manifest: &FeatureManifest,
        _out: &mut Vec<UnreportedIssue>,
    ) {
    }

    /// Lint an object definition.
    fn lint_object(&self, _object: &ObjectDef, _out: &mut Vec<UnreportedIssue>) {}

    /// Lint an enumeration.
    fn lint_enum(&self, _enum_def: &EnumDef, _out: &mut Vec<UnreportedIssue>) {}

    /// Lint a manifest.
    fn lint_manifest(&self, _manifest: &FeatureManifest, _out: &mut Vec<UnreportedIssue>) {}
}

define_categories! {
    Metadata = "metadata" => metadata::MetadataLints;
    Documentation = "documentation" => documentation::DocumentationLints;
    Naming = "naming" => naming::Naming;
    Design = "design" => design::DesignLints;
    Lints = "lints" => LintLints;
}

/// Lints about lints, e.g., attempting to suppress unknown lints with no-lint
/// annotations.
struct LintLints;

impl Linter for LintLints {
    fn lints(&self) -> &'static [&'static LintInfo] {
        LINTS
    }

    fn lint_feature(
        &self,
        feature: &FeatureDef,
        _manifest: &FeatureManifest,
        out: &mut Vec<UnreportedIssue>,
    ) {
        lint_feature_no_lint_annotation(
            feature.metadata.no_lint.iter().map(String::as_str),
            feature_path(feature),
            out,
        );
    }
}

/// Information about a lint.
#[derive(Debug)]
pub struct LintInfo {
    /// The name of the lint.
    pub name: &'static str,

    /// A detailed description of the issue reported by the lint.
    pub description: &'static str,

    /// Instructions on how to fix issues reported by the lint.
    pub help: &'static str,

    /// The category the lint belongs to.
    pub category: LintCategory,
}

lazy_static::lazy_static! {
    /// All the lints that nimbus-fml lint can report.
    pub static ref ALL_LINTS: Vec<&'static LintInfo> = LINTERS
        .iter()
        .flat_map(|linter| linter.lints())
        .copied()
        .collect();
}

pub fn find_lint(name: &str) -> Option<&'static LintInfo> {
    ALL_LINTS.iter().find(|l| l.name == name).copied()
}

/// The location of an issue in a manifest.
#[derive(Debug, Clone)]
pub(crate) struct Location {
    /// The name of the feature, object, or enum.
    subject: String,

    /// The member of the subject.
    ///
    /// This is only Some for feature variables and enum variants.
    member: Option<String>,
}

impl Location {
    fn subject(subject: String) -> Self {
        Self {
            subject,
            member: None,
        }
    }

    fn member(subject: String, member: String) -> Self {
        Self {
            subject,
            member: Some(member),
        }
    }

    pub(crate) fn is_member(&self) -> bool {
        self.member.is_some()
    }
}

/// An issue that has not yet been reported.
///
/// If it is a feature lint, it may yet be suppressed by a `no-lint` annotation
/// on that feature.
#[derive(Debug, Clone)]
pub(crate) struct UnreportedIssue {
    lint: &'static LintInfo,
    location: Location,
    message: String,
}

impl UnreportedIssue {
    pub(crate) fn new(
        lint: &'static LintInfo,
        location: Location,
        message: impl Into<String>,
    ) -> UnreportedIssue {
        Self {
            lint,
            location,
            message: message.into(),
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct Issue {
    pub lint: &'static str,

    /// The module that the issue was reported in.
    ///
    /// Only set when the issue was reported in an imported manifest.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub module: Option<String>,

    /// e.g. ``feature `homescreen` ``.
    pub subject: String,

    /// e.g. ``variable `enabled` ``.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub member: Option<String>,
    pub message: String,
}

#[derive(Debug, Clone, Default)]
pub struct LintReport {
    pub issues: Vec<Issue>,
    /// How many issues were suppressed by `no-lint` annotations.
    pub suppressed: usize,
}

impl LintReport {
    pub fn is_empty(&self) -> bool {
        self.issues.is_empty()
    }

    pub fn issue_count(&self) -> usize {
        self.issues.len()
    }

    /// How many features, objects and enums have issues.
    pub fn subject_count(&self) -> usize {
        self.issues
            .iter()
            .map(|f| (&f.module, &f.subject))
            .collect::<HashSet<_>>()
            .len()
    }

    /// The lints that fired, in registration order.
    pub fn triggered_lints(&self) -> Vec<&'static LintInfo> {
        let fired: HashSet<_> = self.issues.iter().map(|f| f.lint).collect();
        ALL_LINTS
            .iter()
            .filter(|l| fired.contains(l.name))
            .copied()
            .collect()
    }

    pub fn lint_module(&mut self, fm: &FeatureManifest, module: Option<String>) {
        for feature in fm.iter_feature_defs() {
            let mut unreported = Vec::new();
            for linter in LINTERS {
                linter.lint_feature(feature, fm, &mut unreported);
            }

            let allowed_lints: HashSet<&str> = feature
                .metadata
                .no_lint
                .iter()
                .map(String::as_str)
                .collect();

            self.report_issues(unreported, &allowed_lints, module.as_deref());
        }

        let allowed_lints = HashSet::new();

        for object in fm.iter_object_defs() {
            let mut unreported = Vec::new();
            for linter in LINTERS {
                linter.lint_object(object, &mut unreported);
            }
            self.report_issues(unreported, &allowed_lints, module.as_deref());
        }

        for enum_def in fm.iter_enum_defs() {
            let mut unreported = Vec::new();
            for linter in LINTERS {
                linter.lint_enum(enum_def, &mut unreported);
            }
            self.report_issues(unreported, &allowed_lints, module.as_deref());
        }

        let mut unreported = Vec::new();
        for linter in LINTERS {
            linter.lint_manifest(fm, &mut unreported);
        }

        self.report_issues(unreported, &allowed_lints, module.as_deref());
    }

    fn report_issues(
        &mut self,
        unreported: Vec<UnreportedIssue>,
        allowed_lints: &HashSet<&str>,
        module: Option<&str>,
    ) {
        for issue in unreported {
            let name = issue.lint.name;

            if allowed_lints.contains(name) {
                self.suppressed += 1;
                continue;
            }

            self.issues.push(Issue {
                lint: name,
                module: module.map(Into::into),
                subject: issue.location.subject,
                member: issue.location.member,
                message: issue.message,
            });
        }
    }
}

/// Run the lints against a manifest that has already been through
/// [`FeatureManifest::validate_manifest`].
pub fn lint_manifest(fm: &FeatureManifest) -> LintReport {
    let mut report = LintReport::default();

    report.lint_module(fm, None);

    for (id, child) in &fm.all_imports {
        report.lint_module(child, Some(id.to_string()));
    }

    // Group issues by location.
    report.issues.sort_by(|a, b| {
        (&a.module, &a.subject, a.lint, &a.member).cmp(&(&b.module, &b.subject, b.lint, &b.member))
    });

    report
}

fn lint_feature_no_lint_annotation<'a>(
    names: impl IntoIterator<Item = &'a str>,
    location: Location,
    out: &mut Vec<UnreportedIssue>,
) {
    for name in names {
        if find_lint(name).is_none() {
            out.push(UnreportedIssue::new(
                &UNKNOWN_LINT,
                location.clone(),
                format!("`no-lint` names `{name}`, which isn't a lint"),
            ));
        }
    }
}

/// `Option<Boolean>`, as gecko-pref backed variables use, is still a boolean.
pub(crate) fn is_boolean(typ: &TypeRef) -> bool {
    match typ {
        TypeRef::Boolean => true,
        TypeRef::Option(inner) => is_boolean(inner),
        _ => false,
    }
}

pub(crate) fn feature_path(feature: &FeatureDef) -> Location {
    Location::subject(format!("feature `{}`", feature.name))
}

pub(crate) fn variable_path(feature: &FeatureDef, prop: &PropDef) -> Location {
    Location::member(
        format!("feature `{}`", feature.name),
        format!("variable `{}`", prop.name),
    )
}

pub(crate) fn object_path(object: &ObjectDef) -> Location {
    Location::subject(format!("object `{}`", object.name))
}

pub(crate) fn object_field_path(object: &ObjectDef, prop: &PropDef) -> Location {
    Location::member(
        format!("object `{}`", object.name),
        format!("field `{}`", prop.name),
    )
}

pub(crate) fn enum_path(enum_def: &EnumDef) -> Location {
    Location::subject(format!("enum `{}`", enum_def.name))
}

pub(crate) fn enum_variant_path(enum_def: &EnumDef, variant: &str) -> Location {
    Location::member(
        format!("enum `{}`", enum_def.name),
        format!("variant `{variant}`"),
    )
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    use crate::intermediate_representation::{FeatureDef, PropDef, TypeRef};
    use serde_json::json;

    #[test]
    fn test_lint_names_are_unique_and_well_formed() {
        let mut seen = HashSet::new();
        for lint in ALL_LINTS.iter() {
            assert!(
                seen.insert(lint.name),
                "{} is registered more than once",
                lint.name
            );
            assert!(
                lint.name
                    .chars()
                    .all(|c| c.is_ascii_uppercase() || c.is_ascii_digit() || c == '_'),
                "{} isn't SCREAMING_SNAKE_CASE",
                lint.name
            );
            assert!(
                lint.description.ends_with('.'),
                "{}'s description should be a sentence",
                lint.name
            );
            assert!(
                lint.help.ends_with('.'),
                "{}'s help should be a sentence",
                lint.name
            );
        }
    }

    /// `define_categories!` makes owning a category the only way to be run, so what
    /// is left to check is that no two modules share one.
    #[test]
    fn test_each_linter_owns_one_category() {
        let mut owned = HashSet::new();

        for linter in LINTERS {
            let declared = linter.lints();
            assert!(!declared.is_empty(), "a linter declares no lints");

            let categories: HashSet<_> = declared.iter().map(|l| l.category).collect();
            assert_eq!(
                categories.len(),
                1,
                "{:?} spans more than one category",
                declared.iter().map(|l| l.name).collect::<Vec<_>>()
            );

            assert!(
                owned.insert(declared[0].category),
                "two linters declare {} lints; one of them is never run",
                declared[0].category.as_str()
            );
        }
    }

    /// No metadata, no description, no variables.
    fn empty_feature(name: &str) -> FeatureDef {
        FeatureDef::new(name, "", Default::default(), false)
    }

    #[test]
    fn test_every_lint_is_registered() {
        for lint in ALL_LINTS.iter() {
            assert_eq!(find_lint(lint.name).map(|l| l.name), Some(lint.name));
        }
    }

    #[test]
    fn test_suppressed_issues_are_counted() {
        let mut suppressed = empty_feature("suppressed");
        suppressed.metadata.no_lint = vec!["NO_VARIABLES".to_string()];

        let mut fm = FeatureManifest::default();
        fm.add_feature(suppressed);

        let report = lint_manifest(&fm);
        assert_eq!(report.suppressed, 1);
    }

    #[test]
    fn test_issues_are_reported_once_per_lint() {
        let mut fm = FeatureManifest::default();
        fm.add_feature(empty_feature("my-feature"));

        let report = lint_manifest(&fm);
        let mut lints: Vec<_> = report.issues.iter().map(|f| f.lint).collect();
        lints.sort_unstable();
        let deduped = lints.iter().collect::<HashSet<_>>();
        assert_eq!(lints.len(), deduped.len());

        assert!(lints.contains(&"NO_VARIABLES"));
        assert!(lints.contains(&"MISSING_DESCRIPTION"));
        assert!(lints.contains(&"MISSING_CONTACTS"));
    }

    #[test]
    fn test_feature_suppressions() {
        let mut suppressed = empty_feature("suppressed");
        suppressed.metadata.no_lint = vec!["NO_VARIABLES".to_string()];

        let mut fm = FeatureManifest::default();
        fm.add_feature(suppressed);
        fm.add_feature(empty_feature("not-suppressed"));

        let report = lint_manifest(&fm);
        let features: Vec<_> = report
            .issues
            .iter()
            .filter(|f| f.lint == "NO_VARIABLES")
            .map(|f| f.subject.as_str())
            .collect();
        assert_eq!(features, vec!["feature `not-suppressed`"]);
    }

    #[test]
    fn test_unknown_lint_names_in_the_manifest_are_reported() {
        let mut feature = empty_feature("my-feature");
        feature.metadata.no_lint = vec!["NOT_A_LINT".to_string()];

        let mut fm = FeatureManifest::default();
        fm.add_feature(feature);

        let report = lint_manifest(&fm);
        assert!(
            report
                .issues
                .iter()
                .any(|f| f.lint == "UNKNOWN_LINT" && f.message.contains("NOT_A_LINT"))
        );
    }

    #[test]
    fn test_a_well_formed_feature_is_quiet() {
        let mut feature = FeatureDef::new(
            "my-feature",
            "Controls the shape and behaviour of the widget on the home screen.",
            vec![
                PropDef::with_doc(
                    "enabled",
                    "Whether the widget is shown on the home screen at all.",
                    &TypeRef::Boolean,
                    &json!(false),
                ),
                PropDef::with_doc(
                    "max-rows",
                    "The largest number of rows the widget is allowed to grow to.",
                    &TypeRef::Int,
                    &json!(3),
                ),
            ],
            false,
        );
        feature.metadata.contacts = vec!["email@example.com".into()];
        let mut fm = FeatureManifest::default();
        fm.add_feature(feature);

        // Metadata lints are expected; nothing else should fire.
        let report = lint_manifest(&fm);
        assert!(report.is_empty(), "unexpected issues: {:?}", report.issues);
    }
}
