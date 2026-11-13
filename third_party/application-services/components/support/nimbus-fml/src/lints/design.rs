/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Lints about the shape of a feature. A feature can pass validation and still be
//! one that can't be switched off, can't be configured, or can't be filled in
//! without reading the source.

use std::collections::{BTreeMap, HashSet};

use super::{
    enum_path, feature_path, is_boolean, object_path, variable_path, LintInfo, Linter, RawFinding,
};
use crate::intermediate_representation::{
    EnumDef, FeatureDef, FeatureManifest, ObjectDef, PropDef, TypeRef,
};

define_lints! {
    NO_VARIABLES: Design, Warning =
        "Features should have something an experiment can change.",
        "An experiment on a feature with no variables can only enrol users; it can't change what they see. Add at least a boolean `enabled` variable.";
    MISSING_ENABLED_VARIABLE: Design, Warning =
        "Features should be able to be switched off remotely.",
        "Features should be able to be switched off without shipping new code. Add `enabled: { type: Boolean }`, default it to the behaviour that ships today, and check it before using the rest of the feature.";
    TOO_MANY_VARIABLES: Design, Warning =
        "Features with a lot of variables are hard to configure correctly.",
        "Every variable is something an experiment owner has to understand. Consider splitting the feature up, or grouping related variables into objects.";
    STRINGLY_TYPED: Design, Warning =
        "Values with a fixed set of options should be enums, not strings.",
        "Declare an enum and use that as the type: Experimenter can then offer the options and reject anything else, instead of passing a typo through to the app. A `Map<String, String>` can't be checked at all; consider an object for the values, or a `StringAlias` for the keys.";
    DEEP_NESTING: Design, Warning =
        "Deeply nested configuration is hard to write by hand.",
        "Experiment owners write these values out as JSON by hand. Consider flattening the value, or adding an `examples` block showing a complete one.";
    TRIVIAL_ENUM: Design, Warning =
        "An enum should offer a choice.",
        "An enum that can only take one value can't be varied by an experiment. Add the other variants, or use a boolean.";
    UNUSED_TYPE: Design, Warning =
        "Objects and enums should be used by at least one feature.",
        "Unused types are still generated into Kotlin and Swift, and still have to be maintained. Delete it, or use it.";
}

const MAX_VARIABLES: usize = 25;

/// A feature's variables are level 1, the fields of an object they hold level 2.
const MAX_NESTING_DEPTH: usize = 3;

/// Names that suggest a value is one of a fixed set of options.
const ENUMERABLE_SUFFIXES: &[&str] = &[
    "alignment",
    "behavior",
    "behaviour",
    "direction",
    "kind",
    "layout",
    "mode",
    "placement",
    "position",
    "state",
    "strategy",
    "style",
    "theme",
    "treatment",
    "type",
    "variant",
];

pub(crate) struct Design;

impl Linter for Design {
    fn lints(&self) -> &'static [&'static LintInfo] {
        LINTS
    }

    fn check_feature(
        &self,
        feature: &FeatureDef,
        manifest: &FeatureManifest,
        out: &mut Vec<RawFinding>,
    ) {
        check_feature(feature, manifest, out);
    }

    fn check_enum(&self, enum_def: &EnumDef, out: &mut Vec<RawFinding>) {
        check_enum(enum_def, out);
    }

    fn check_manifest(&self, manifest: &FeatureManifest, out: &mut Vec<RawFinding>) {
        check_manifest(manifest, out);
    }
}

fn check_feature(feature: &FeatureDef, manifest: &FeatureManifest, out: &mut Vec<RawFinding>) {
    if feature.props.is_empty() {
        out.push(RawFinding::new(
            &NO_VARIABLES,
            feature_path(feature),
            "This feature has no variables",
        ));
        // The rest is about variables this feature doesn't have.
        return;
    }

    if !feature.props.iter().any(is_enabled_variable) {
        out.push(RawFinding::new(
            &MISSING_ENABLED_VARIABLE,
            feature_path(feature),
            "This feature has no boolean `enabled` variable",
        ));
    }

    if feature.props.len() > MAX_VARIABLES {
        out.push(RawFinding::new(
            &TOO_MANY_VARIABLES,
            feature_path(feature),
            format!(
                "This feature has {} variables (the limit is {MAX_VARIABLES})",
                feature.props.len()
            ),
        ));
    }

    for prop in &feature.props {
        check_stringly_typed(feature, prop, out);
        check_nesting(feature, prop, &manifest.obj_defs, out);
    }
}

fn check_enum(enum_def: &EnumDef, out: &mut Vec<RawFinding>) {
    if enum_def.variants.len() < 2 {
        out.push(RawFinding::new(
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

fn check_manifest(manifest: &FeatureManifest, out: &mut Vec<RawFinding>) {
    // A manifest with no features is a library of types to include elsewhere.
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
            out.push(RawFinding::new(
                &UNUSED_TYPE,
                object_path(object),
                "No feature in this manifest uses this object",
            ));
        }
    }

    for enum_def in manifest.iter_enum_defs() {
        if !used.contains(&TypeRef::Enum(enum_def.name.clone())) {
            out.push(RawFinding::new(
                &UNUSED_TYPE,
                enum_path(enum_def),
                "No feature in this manifest uses this enum",
            ));
        }
    }
}

/// Record every type reachable from `typ`. Unlike `TypeQuery`, this tolerates an
/// undefined object rather than panicking.
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

fn is_enabled_variable(prop: &PropDef) -> bool {
    is_boolean(&prop.typ) && (prop.name == "enabled" || prop.name.ends_with("-enabled"))
}

fn check_stringly_typed(feature: &FeatureDef, prop: &PropDef, out: &mut Vec<RawFinding>) {
    // A string-alias already says "one of a known set of strings".
    if prop.string_alias.is_some() {
        return;
    }

    if let TypeRef::StringMap(values) = &prop.typ {
        if matches!(**values, TypeRef::String) {
            out.push(RawFinding::new(
                &STRINGLY_TYPED,
                variable_path(feature, prop),
                format!(
                    "`{}` is a `Map<String, String>`, so neither its keys nor its values can be checked",
                    prop.name
                ),
            ));
            return;
        }
    }

    if !is_string(&prop.typ) {
        return;
    }

    let suffix = prop
        .name
        .rsplit('-')
        .next()
        .map(str::to_ascii_lowercase)
        .unwrap_or_default();
    // Plurals count: `supported-modes` is as enumerable as `mode`.
    let singular = suffix.strip_suffix('s').unwrap_or(&suffix);

    if ENUMERABLE_SUFFIXES.contains(&singular) {
        out.push(RawFinding::new(
            &STRINGLY_TYPED,
            variable_path(feature, prop),
            format!(
                "`{}` is a `{}`, but its name suggests it is one of a fixed set of values",
                prop.name, prop.typ
            ),
        ));
    }
}

fn is_string(typ: &TypeRef) -> bool {
    match typ {
        TypeRef::String => true,
        TypeRef::Option(inner) | TypeRef::List(inner) => is_string(inner),
        _ => false,
    }
}

fn check_nesting(
    feature: &FeatureDef,
    prop: &PropDef,
    objects: &BTreeMap<String, ObjectDef>,
    out: &mut Vec<RawFinding>,
) {
    let mut visiting = HashSet::new();
    let depth = 1 + type_depth(&prop.typ, objects, &mut visiting);
    if depth > MAX_NESTING_DEPTH {
        out.push(RawFinding::new(
            &DEEP_NESTING,
            variable_path(feature, prop),
            format!(
                "The value of `{}` is {depth} levels deep (the limit is {MAX_NESTING_DEPTH})",
                prop.name
            ),
        ));
    }
}

/// How many levels of object are underneath this type.
fn type_depth(
    typ: &TypeRef,
    objects: &BTreeMap<String, ObjectDef>,
    visiting: &mut HashSet<String>,
) -> usize {
    match typ {
        TypeRef::Option(inner) | TypeRef::List(inner) | TypeRef::StringMap(inner) => {
            type_depth(inner, objects, visiting)
        }
        TypeRef::EnumMap(_, values) => type_depth(values, objects, visiting),
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
                        .map(|p| type_depth(&p.typ, objects, visiting))
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
        check_feature(feature, manifest, &mut out);
        out.iter().map(|f| f.lint.name).collect()
    }

    #[test]
    fn test_no_variables() {
        let findings = lints(&feature(Default::default()));
        assert_eq!(findings, vec!["NO_VARIABLES"]);
    }

    #[test]
    fn test_missing_enabled_variable() {
        assert!(lints(&feature(vec![prop("max-rows", &TypeRef::Int)]))
            .contains(&"MISSING_ENABLED_VARIABLE"));

        for name in ["enabled", "sync-enabled"] {
            assert!(
                !lints(&feature(vec![prop(name, &TypeRef::Boolean)]))
                    .contains(&"MISSING_ENABLED_VARIABLE"),
                "{name} should count as an enabled variable"
            );
        }

        // `Option<Boolean>`, as gecko-pref backed variables use, counts too.
        assert!(!lints(&feature(vec![prop(
            "enabled",
            &TypeRef::Option(Box::new(TypeRef::Boolean))
        )]))
        .contains(&"MISSING_ENABLED_VARIABLE"));

        // A string called `enabled` doesn't.
        assert!(lints(&feature(vec![prop("enabled", &TypeRef::String)]))
            .contains(&"MISSING_ENABLED_VARIABLE"));
    }

    #[test]
    fn test_too_many_variables() {
        let props = (0..=MAX_VARIABLES)
            .map(|i| prop(&format!("variable-{i}"), &TypeRef::Int))
            .collect();
        assert!(lints(&feature(props)).contains(&"TOO_MANY_VARIABLES"));
    }

    #[test]
    fn test_stringly_typed() {
        assert!(
            lints(&feature(vec![prop("button-style", &TypeRef::String)]))
                .contains(&"STRINGLY_TYPED")
        );
        assert!(lints(&feature(vec![prop(
            "supported-modes",
            &TypeRef::List(Box::new(TypeRef::String))
        )]))
        .contains(&"STRINGLY_TYPED"));
        assert!(lints(&feature(vec![prop(
            "overrides",
            &TypeRef::StringMap(Box::new(TypeRef::String))
        )]))
        .contains(&"STRINGLY_TYPED"));

        // An enum is what the lint is asking for.
        assert!(!lints(&feature(vec![prop(
            "button-style",
            &TypeRef::Enum("ButtonStyle".to_string())
        )]))
        .contains(&"STRINGLY_TYPED"));

        // So is a string-alias.
        let aliased = PropDef::with_string_alias(
            "player-type",
            &TypeRef::String,
            &json!(null),
            &TypeRef::StringAlias("PlayerType".to_string()),
        );
        assert!(!lints(&feature(vec![aliased])).contains(&"STRINGLY_TYPED"));

        // A name that doesn't suggest fixed options is fine as a string.
        assert!(
            !lints(&feature(vec![prop("button-label", &TypeRef::String)]))
                .contains(&"STRINGLY_TYPED")
        );
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
        check_enum(&EnumDef::new("OnlyOne", &["only"]), &mut out);
        assert_eq!(
            out.iter().map(|f| f.lint.name).collect::<Vec<_>>(),
            vec!["TRIVIAL_ENUM"]
        );

        let mut out = Vec::new();
        check_enum(&EnumDef::new("TwoOfThem", &["this", "that"]), &mut out);
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
        check_manifest(&manifest, &mut out);
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
        check_manifest(&manifest, &mut out);
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
        check_manifest(&manifest, &mut out);
        assert!(out.is_empty());
    }
}
