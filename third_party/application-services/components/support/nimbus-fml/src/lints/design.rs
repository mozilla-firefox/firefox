// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Lints about feature shape.

use std::collections::{BTreeMap, HashSet};

use crate::intermediate_representation::{
    EnumDef, FeatureDef, FeatureManifest, ObjectDef, PropDef, TypeRef,
};
use crate::lints::{
    LintInfo, Linter, UnreportedIssue, define_lints, enum_path, feature_path, object_path,
    variable_path,
};

define_lints! {
    NO_VARIABLES: Design =
        "Features should have something an experiment can change.",
        "An experiment on a feature with no variables can only enrol users; it can't change what they see. Add at least a boolean `enabled` variable.";
    TOO_MANY_VARIABLES: Design =
        "Features with a lot of variables are hard to configure correctly.",
        "Every variable is something an experiment owner has to understand. Consider splitting the feature up, or grouping related variables into objects.";
    DEEP_NESTING: Design =
        "Deeply nested configuration is hard to write by hand.",
        "Experiment owners write these values out as JSON by hand. Consider flattening the value, or adding an `examples` block showing a complete one.";
    TRIVIAL_ENUM: Design =
        "An enum should offer a choice.",
        "An enum that can only take one value can't be varied by an experiment. Add the other variants, or use a boolean.";
    UNUSED_TYPE: Design =
        "Objects and enums should be used by at least one feature.",
        "Unused types are still generated into Kotlin and Swift, and still have to be maintained. Delete it, or use it.";
}

const MAX_VARIABLES: usize = 25;

/// How many nested levels of objects are allowed.
///
/// A feature with no object types has a depth of 1.
const MAX_NESTING_DEPTH: usize = 3;

/// Lints about the design of features.
pub(crate) struct DesignLints;

impl Linter for DesignLints {
    fn lints(&self) -> &'static [&'static LintInfo] {
        LINTS
    }

    fn lint_feature(
        &self,
        feature: &FeatureDef,
        manifest: &FeatureManifest,
        out: &mut Vec<UnreportedIssue>,
    ) {
        lint_feature(feature, manifest, out);
    }

    fn lint_enum(&self, enum_def: &EnumDef, out: &mut Vec<UnreportedIssue>) {
        lint_enum(enum_def, out);
    }

    fn lint_manifest(&self, manifest: &FeatureManifest, out: &mut Vec<UnreportedIssue>) {
        lint_manifest(manifest, out);
    }
}

fn lint_feature(feature: &FeatureDef, manifest: &FeatureManifest, out: &mut Vec<UnreportedIssue>) {
    if feature.props.is_empty() {
        out.push(UnreportedIssue::new(
            &NO_VARIABLES,
            feature_path(feature),
            "This feature has no variables",
        ));
        // The rest is about variables this feature doesn't have.
        return;
    }

    if feature.props.len() > MAX_VARIABLES {
        out.push(UnreportedIssue::new(
            &TOO_MANY_VARIABLES,
            feature_path(feature),
            format!(
                "This feature has {} variables (the limit is {MAX_VARIABLES})",
                feature.props.len()
            ),
        ));
    }

    for prop in &feature.props {
        lint_feature_nesting(feature, prop, &manifest.obj_defs, out);
    }
}

fn lint_enum(enum_def: &EnumDef, out: &mut Vec<UnreportedIssue>) {
    if enum_def.variants.len() < 2 {
        out.push(UnreportedIssue::new(
            &TRIVIAL_ENUM,
            enum_path(enum_def),
            format!(
                "This enum has {} variant{}",
                enum_def.variants.len(),
                if enum_def.variants.len() == 1 {
                    ""
                } else {
                    "s"
                }
            ),
        ));
    }
}

fn lint_manifest(manifest: &FeatureManifest, out: &mut Vec<UnreportedIssue>) {
    if manifest.feature_defs.is_empty() {
        return;
    }

    let mut used = HashSet::new();
    for feature in manifest.iter_feature_defs() {
        for prop in &feature.props {
            mark_used(&prop.typ, &manifest.obj_defs, &mut used);
        }
    }

    for object in manifest.iter_object_defs() {
        if !used.contains(&TypeRef::Object(object.name.clone())) {
            out.push(UnreportedIssue::new(
                &UNUSED_TYPE,
                object_path(object),
                "No feature in this manifest uses this object",
            ));
        }
    }

    for enum_def in manifest.iter_enum_defs() {
        if !used.contains(&TypeRef::Enum(enum_def.name.clone())) {
            out.push(UnreportedIssue::new(
                &UNUSED_TYPE,
                enum_path(enum_def),
                "No feature in this manifest uses this enum",
            ));
        }
    }
}

/// Record every type reachable from `typ`.
fn mark_used(typ: &TypeRef, objects: &BTreeMap<String, ObjectDef>, used: &mut HashSet<TypeRef>) {
    if !used.insert(typ.clone()) {
        return;
    }

    match typ {
        TypeRef::Option(inner) | TypeRef::List(inner) | TypeRef::StringMap(inner) => {
            mark_used(inner, objects, used)
        }
        TypeRef::EnumMap(keys, values) => {
            mark_used(keys, objects, used);
            mark_used(values, objects, used);
        }
        TypeRef::Object(name) => {
            if let Some(object) = objects.get(name) {
                for prop in &object.props {
                    mark_used(&prop.typ, objects, used);
                }
            }
        }
        _ => {}
    }
}

fn lint_feature_nesting(
    feature: &FeatureDef,
    prop: &PropDef,
    objects: &BTreeMap<String, ObjectDef>,
    out: &mut Vec<UnreportedIssue>,
) {
    let mut visiting = HashSet::new();
    let depth = 1 + compute_type_depth(&prop.typ, objects, &mut visiting);
    if depth > MAX_NESTING_DEPTH {
        out.push(UnreportedIssue::new(
            &DEEP_NESTING,
            variable_path(feature, prop),
            format!(
                "The value of `{}` is {depth} levels deep (the limit is {MAX_NESTING_DEPTH})",
                prop.name
            ),
        ));
    }
}

/// How many levels of objects are underneath this type.
fn compute_type_depth(
    typ: &TypeRef,
    objects: &BTreeMap<String, ObjectDef>,
    visiting: &mut HashSet<String>,
) -> usize {
    match typ {
        TypeRef::Option(inner) | TypeRef::List(inner) | TypeRef::StringMap(inner) => {
            compute_type_depth(inner, objects, visiting)
        }
        TypeRef::EnumMap(_, values) => compute_type_depth(values, objects, visiting),
        TypeRef::Object(name) => {
            if !visiting.insert(name.clone()) {
                // Stop at a cycle; it is already deep enough to report.
                return MAX_NESTING_DEPTH;
            }
            let depth = objects
                .get(name)
                .map(|o| {
                    o.props
                        .iter()
                        .map(|p| compute_type_depth(&p.typ, objects, visiting))
                        .max()
                        .unwrap_or_default()
                })
                .unwrap_or_default();
            visiting.remove(name);
            1 + depth
        }
        _ => 0,
    }
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    use serde_json::json;

    fn prop(name: &str, typ: &TypeRef) -> PropDef {
        PropDef::with_doc(name, "A description of the variable.", typ, &json!(null))
    }

    fn feature(props: Vec<PropDef>) -> FeatureDef {
        FeatureDef::new("my-feature", "A description.", props, false)
    }

    fn lints(feature: &FeatureDef) -> Vec<&'static str> {
        lints_with(feature, &Default::default())
    }

    fn lints_with(feature: &FeatureDef, manifest: &FeatureManifest) -> Vec<&'static str> {
        let mut out = Vec::new();
        lint_feature(feature, manifest, &mut out);
        out.iter().map(|f| f.lint.name).collect()
    }

    #[test]
    fn test_no_variables() {
        let issues = lints(&feature(Default::default()));
        assert_eq!(issues, vec!["NO_VARIABLES"]);
    }

    #[test]
    fn test_too_many_variables() {
        let props = (0..=MAX_VARIABLES)
            .map(|i| prop(&format!("variable-{i}"), &TypeRef::Int))
            .collect();
        assert!(lints(&feature(props)).contains(&"TOO_MANY_VARIABLES"));
    }

    #[test]
    fn test_deep_nesting() {
        let mut manifest = FeatureManifest {
            obj_defs: ObjectDef::into_map(&[
                ObjectDef::new(
                    "Outer",
                    &[prop("middle", &TypeRef::Object("Middle".into()))],
                ),
                ObjectDef::new("Middle", &[prop("inner", &TypeRef::Object("Inner".into()))]),
                ObjectDef::new("Inner", &[prop("label", &TypeRef::String)]),
            ]),
            ..Default::default()
        };

        // feature › Outer › Middle › Inner is 4 levels.
        let outer = feature(vec![prop("outer", &TypeRef::Object("Outer".into()))]);
        assert!(lints_with(&outer, &manifest).contains(&"DEEP_NESTING"));

        // feature › Middle › Inner is 3.
        let middle = feature(vec![prop("middle", &TypeRef::Object("Middle".into()))]);
        assert!(!lints_with(&middle, &manifest).contains(&"DEEP_NESTING"));

        // A list of objects is as deep as the objects in it.
        let list_of_outers = feature(vec![prop(
            "outers",
            &TypeRef::List(Box::new(TypeRef::Object("Outer".into()))),
        )]);
        assert!(lints_with(&list_of_outers, &manifest).contains(&"DEEP_NESTING"));

        // A cycle terminates.
        manifest.obj_defs.insert(
            "Inner".to_string(),
            ObjectDef::new("Inner", &[prop("outer", &TypeRef::Object("Outer".into()))]),
        );
        assert!(lints_with(&outer, &manifest).contains(&"DEEP_NESTING"));
    }

    #[test]
    fn test_trivial_enum() {
        let mut out = Vec::new();
        lint_enum(&EnumDef::new("OnlyOne", &["only"]), &mut out);
        assert_eq!(
            out.iter().map(|f| f.lint.name).collect::<Vec<_>>(),
            vec!["TRIVIAL_ENUM"]
        );

        let mut out = Vec::new();
        lint_enum(&EnumDef::new("TwoOfThem", &["this", "that"]), &mut out);
        assert!(out.is_empty());
    }

    #[test]
    fn test_unused_type() {
        let used = TypeRef::Object("Used".into());
        let mut manifest = FeatureManifest {
            obj_defs: ObjectDef::into_map(&[
                ObjectDef::new("Used", &[prop("style", &TypeRef::Enum("Style".into()))]),
                ObjectDef::new("Unused", &[prop("label", &TypeRef::String)]),
            ]),
            enum_defs: EnumDef::into_map(&[
                EnumDef::new("Style", &["this", "that"]),
                EnumDef::new("UnusedStyle", &["this", "that"]),
            ]),
            ..Default::default()
        };
        manifest.add_feature(feature(vec![prop("used", &used)]));

        let mut out = Vec::new();
        lint_manifest(&manifest, &mut out);
        let paths: Vec<_> = out.iter().map(|f| f.location.subject.as_str()).collect();
        assert_eq!(paths, vec!["object `Unused`", "enum `UnusedStyle`"]);
    }

    #[test]
    fn test_unused_types_survive_a_dangling_object_reference() {
        // `Missing` is never declared. Validation catches that; linting shouldn't
        // panic on it.
        let mut manifest = FeatureManifest {
            obj_defs: ObjectDef::into_map(&[ObjectDef::new(
                "Unused",
                &[prop("label", &TypeRef::String)],
            )]),
            ..Default::default()
        };
        manifest.add_feature(feature(vec![prop(
            "dangling",
            &TypeRef::Object("Missing".into()),
        )]));

        let mut out = Vec::new();
        lint_manifest(&manifest, &mut out);
        assert_eq!(
            out.iter()
                .map(|f| f.location.subject.as_str())
                .collect::<Vec<_>>(),
            vec!["object `Unused`"]
        );
    }

    #[test]
    fn test_types_are_used_if_a_manifest_has_no_features() {
        let manifest = FeatureManifest {
            obj_defs: ObjectDef::into_map(&[ObjectDef::new(
                "Exported",
                &[prop("label", &TypeRef::String)],
            )]),
            ..Default::default()
        };

        let mut out = Vec::new();
        lint_manifest(&manifest, &mut out);
        assert!(out.is_empty());
    }
}
