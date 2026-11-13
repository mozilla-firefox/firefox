/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Lints about names. Feature ids and variable names are typed by hand into
//! Experimenter branches, so they should be predictable.

use lazy_static::lazy_static;
use regex::Regex;

use super::{
    enum_path, enum_variant_path, feature_path, is_boolean, object_field_path, object_path,
    variable_path, LintInfo, Linter, Location, RawFinding,
};
use crate::intermediate_representation::{
    EnumDef, FeatureDef, FeatureManifest, ObjectDef, PropDef, TypeRef,
};

define_lints! {
    FEATURE_NAME_CASING: Naming, Warning =
        "Feature ids should be kebab-case.",
        "Feature ids are typed by hand into Experimenter.";
    VARIABLE_NAME_CASING: Naming, Warning =
        "Variable and field names should be kebab-case.",
        "Variables are written out as JSON keys in Experimenter.";
    TYPE_NAME_CASING: Naming, Warning =
        "Objects and enums should be UpperCamelCase.",
        "Objects and enums become classes and types in the generated Kotlin and Swift.";
    ENUM_VARIANT_CASING: Naming, Warning =
        "Enum variants should be kebab-case.",
        "Variants are written out as JSON strings in Experimenter.";
    COMMON_PREFIX: Naming, Warning =
        "Variables shouldn't repeat the name of the feature they belong to.",
        "Variables are always read together with the feature they belong to, so a shared prefix only makes them longer. Drop it, or group the variables into an object if the prefix is really a thing in its own right.";
    TYPE_IN_NAME: Naming, Warning =
        "Variable names shouldn't repeat the name of their type.",
        "The type is shown next to the name everywhere the name is; repeating it only makes the name longer.";
    NEGATED_BOOLEAN: Naming, Warning =
        "Booleans should be named for what is true, not what is false.",
        "Everyone setting this in an experiment has to work out what `false` means. Name the variable for the behaviour that is switched on, and flip the default.";
}

lazy_static! {
    static ref KEBAB_CASE: Regex = Regex::new(r"^[a-z][a-z0-9]*(-[a-z0-9]+)*$").unwrap();
    static ref UPPER_CAMEL_CASE: Regex = Regex::new(r"^[A-Z][A-Za-z0-9]*$").unwrap();
}

/// Words that read as a predicate rather than a namespace, so sharing one is a
/// convention rather than a prefix to strip.
const PREDICATE_WORDS: &[&str] = &["allow", "can", "has", "is", "should", "show", "use"];

/// Words that make `false` mean the thing happens.
const NEGATIVE_WORDS: &[&str] = &[
    "disable",
    "disabled",
    "disallow",
    "disallowed",
    "dont",
    "hidden",
    "hide",
    "never",
    "no",
    "not",
    "prevent",
    "suppress",
];

pub(crate) struct Naming;

impl Linter for Naming {
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

    fn check_object(&self, object: &ObjectDef, out: &mut Vec<RawFinding>) {
        check_object(object, out);
    }

    fn check_enum(&self, enum_def: &EnumDef, out: &mut Vec<RawFinding>) {
        check_enum(enum_def, out);
    }
}

fn check_feature(feature: &FeatureDef, out: &mut Vec<RawFinding>) {
    if !KEBAB_CASE.is_match(&feature.name) {
        out.push(RawFinding::new(
            &FEATURE_NAME_CASING,
            feature_path(feature),
            format!(
                "`{}` isn't kebab-case{}",
                feature.name,
                rename_to(&feature.name)
            ),
        ));
    }

    for prop in &feature.props {
        check_variable_name(&prop.name, variable_path(feature, prop), out);
        check_type_in_name(prop, variable_path(feature, prop), out);
        check_negated_boolean(prop, variable_path(feature, prop), out);
    }

    check_common_prefix(feature, out);
}

fn check_object(object: &ObjectDef, out: &mut Vec<RawFinding>) {
    if !UPPER_CAMEL_CASE.is_match(&object.name) {
        out.push(RawFinding::new(
            &TYPE_NAME_CASING,
            object_path(object),
            format!("`{}` isn't UpperCamelCase", object.name),
        ));
    }

    for prop in &object.props {
        check_variable_name(&prop.name, object_field_path(object, prop), out);
        check_type_in_name(prop, object_field_path(object, prop), out);
        check_negated_boolean(prop, object_field_path(object, prop), out);
    }
}

fn check_enum(enum_def: &EnumDef, out: &mut Vec<RawFinding>) {
    if !UPPER_CAMEL_CASE.is_match(&enum_def.name) {
        out.push(RawFinding::new(
            &TYPE_NAME_CASING,
            enum_path(enum_def),
            format!("`{}` isn't UpperCamelCase", enum_def.name),
        ));
    }

    for variant in &enum_def.variants {
        if !KEBAB_CASE.is_match(&variant.name) {
            out.push(RawFinding::new(
                &ENUM_VARIANT_CASING,
                enum_variant_path(enum_def, &variant.name),
                format!(
                    "`{}` isn't kebab-case{}",
                    variant.name,
                    rename_to(&variant.name)
                ),
            ));
        }
    }
}

fn check_variable_name(name: &str, path: Location, out: &mut Vec<RawFinding>) {
    if !KEBAB_CASE.is_match(name) {
        out.push(RawFinding::new(
            &VARIABLE_NAME_CASING,
            path,
            format!("`{name}` isn't kebab-case{}", rename_to(name)),
        ));
    }
}

/// `sections-list: List<Section>` says "list" twice.
fn check_type_in_name(prop: &PropDef, path: Location, out: &mut Vec<RawFinding>) {
    let Some((_, last)) = prop.name.rsplit_once('-') else {
        return;
    };
    let last = last.to_ascii_lowercase();

    // `Option<Boolean>` is still a boolean, as far as the name goes.
    let typ = match &prop.typ {
        TypeRef::Option(inner) => inner.as_ref(),
        typ => typ,
    };

    let redundant = match typ {
        TypeRef::Boolean => ["bool", "boolean", "flag"].contains(&last.as_str()),
        TypeRef::Int => ["int", "integer", "num", "number"].contains(&last.as_str()),
        TypeRef::String => ["str", "string"].contains(&last.as_str()),
        TypeRef::List(_) => ["list", "array"].contains(&last.as_str()),
        TypeRef::StringMap(_) | TypeRef::EnumMap(..) => {
            ["map", "dict", "dictionary"].contains(&last.as_str())
        }
        TypeRef::Enum(_) => last == "enum",
        TypeRef::Object(_) => ["obj", "object", "json"].contains(&last.as_str()),
        _ => false,
    };

    if redundant {
        out.push(RawFinding::new(
            &TYPE_IN_NAME,
            path,
            format!(
                "`{}` ends in `-{last}`, but its type is already `{}`",
                prop.name, prop.typ
            ),
        ));
    }
}

/// `disable-sync: false` takes a moment to read; `sync-enabled: true` doesn't.
fn check_negated_boolean(prop: &PropDef, path: Location, out: &mut Vec<RawFinding>) {
    if !is_boolean(&prop.typ) {
        return;
    }

    let negative = prop
        .name
        .split('-')
        .find(|word| NEGATIVE_WORDS.contains(&word.to_ascii_lowercase().as_str()));

    if let Some(word) = negative {
        out.push(RawFinding::new(
            &NEGATED_BOOLEAN,
            path,
            format!("`{}` is a boolean named with `{word}`", prop.name),
        ));
    }
}

/// A feature called `homescreen` doesn't need variables called `homescreen-*`.
fn check_common_prefix(feature: &FeatureDef, out: &mut Vec<RawFinding>) {
    let feature_prefix = format!("{}-", feature.name);
    let mut prefixed = Vec::new();

    for prop in &feature.props {
        if prop.name.starts_with(&feature_prefix) {
            prefixed.push(prop);
        }
    }

    for prop in &prefixed {
        out.push(RawFinding::new(
            &COMMON_PREFIX,
            variable_path(feature, prop),
            format!(
                "`{}` repeats the name of the feature it belongs to; rename it to `{}`",
                prop.name,
                &prop.name[feature_prefix.len()..]
            ),
        ));
    }

    // A prefix shared by every variable is noise even when it isn't the feature name.
    if prefixed.is_empty() && feature.props.len() > 1 {
        if let Some(shared) = shared_prefix(&feature.props) {
            out.push(RawFinding::new(
                &COMMON_PREFIX,
                feature_path(feature),
                format!(
                    "All {} variables of this feature start with `{shared}-`",
                    feature.props.len()
                ),
            ));
        }
    }
}

fn shared_prefix(props: &[PropDef]) -> Option<String> {
    let first = props.first()?.name.split('-').next()?.to_string();
    if PREDICATE_WORDS.contains(&first.as_str()) {
        return None;
    }
    props
        .iter()
        .all(|p| {
            p.name
                .split_once('-')
                .map(|(head, _)| head == first)
                .unwrap_or_default()
        })
        .then_some(first)
}

/// Empty when `heck` can't improve on `name`, as for `9lives`.
fn rename_to(name: &str) -> String {
    use heck::ToKebabCase;
    let kebab = name.to_kebab_case();
    if kebab == name {
        String::new()
    } else {
        format!("; rename it to `{kebab}`")
    }
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    use serde_json::json;

    fn prop(name: &str, typ: &TypeRef) -> PropDef {
        PropDef::with_doc(name, "A description of the variable.", typ, &json!(null))
    }

    fn feature(name: &str, props: Vec<PropDef>) -> FeatureDef {
        FeatureDef::new(name, "A description of the feature.", props, false)
    }

    fn lints(feature: &FeatureDef) -> Vec<&'static str> {
        let mut out = Vec::new();
        check_feature(feature, &mut out);
        out.iter().map(|f| f.lint.name).collect()
    }

    #[test]
    fn test_casing() {
        assert!(lints(&feature(
            "my-feature",
            vec![prop("my-variable", &TypeRef::Int)]
        ))
        .is_empty());

        assert_eq!(
            lints(&feature("myFeature", Default::default())),
            vec!["FEATURE_NAME_CASING"]
        );
        assert_eq!(
            lints(&feature("my_feature", Default::default())),
            vec!["FEATURE_NAME_CASING"]
        );
        assert_eq!(
            lints(&feature(
                "my-feature",
                vec![prop("myVariable", &TypeRef::Int)]
            )),
            vec!["VARIABLE_NAME_CASING"]
        );
    }

    #[test]
    fn test_casing_without_a_suggestion() {
        // `heck` can't improve a name the regex rejects for starting with a digit.
        let findings = lints(&feature("9lives", Default::default()));
        assert_eq!(findings, vec!["FEATURE_NAME_CASING"]);

        let mut out = Vec::new();
        check_feature(&feature("9lives", Default::default()), &mut out);
        assert_eq!(out[0].message, "`9lives` isn't kebab-case");
    }

    #[test]
    fn test_type_name_casing() {
        let mut out = Vec::new();
        check_object(&ObjectDef::new("my-object", &[]), &mut out);
        check_enum(&EnumDef::new("myEnum", &["ok"]), &mut out);
        let names: Vec<_> = out.iter().map(|f| f.lint.name).collect();
        assert_eq!(names, vec!["TYPE_NAME_CASING", "TYPE_NAME_CASING"]);

        let mut out = Vec::new();
        check_enum(&EnumDef::new("MyEnum", &["notKebab"]), &mut out);
        let names: Vec<_> = out.iter().map(|f| f.lint.name).collect();
        assert_eq!(names, vec!["ENUM_VARIANT_CASING"]);
    }

    #[test]
    fn test_object_fields_are_checked_like_variables() {
        let mut out = Vec::new();
        check_object(
            &ObjectDef::new(
                "Section",
                &[
                    prop("sectionTitle", &TypeRef::String),
                    prop("hide-header", &TypeRef::Boolean),
                    prop("row-count-int", &TypeRef::Int),
                ],
            ),
            &mut out,
        );

        assert_eq!(
            out.iter().map(|f| f.lint.name).collect::<Vec<_>>(),
            vec!["VARIABLE_NAME_CASING", "NEGATED_BOOLEAN", "TYPE_IN_NAME"]
        );
    }

    #[test]
    fn test_type_in_name() {
        for (name, typ) in [
            ("enabled-bool", TypeRef::Boolean),
            ("sections-list", TypeRef::List(Box::new(TypeRef::String))),
            ("max-rows-int", TypeRef::Int),
        ] {
            assert!(
                lints(&feature("my-feature", vec![prop(name, &typ)])).contains(&"TYPE_IN_NAME"),
                "{name} should be flagged"
            );
        }

        // Still a boolean when it's optional.
        assert!(lints(&feature(
            "my-feature",
            vec![prop(
                "enabled-bool",
                &TypeRef::Option(Box::new(TypeRef::Boolean))
            )]
        ))
        .contains(&"TYPE_IN_NAME"));

        // The suffix is only redundant if it really is the type.
        assert!(!lints(&feature(
            "my-feature",
            vec![prop("shopping-list", &TypeRef::String)]
        ))
        .contains(&"TYPE_IN_NAME"));
    }

    #[test]
    fn test_negated_boolean() {
        for name in [
            "disable-sync",
            "hide-toolbar",
            "sync-disabled",
            "no-onboarding",
        ] {
            assert!(
                lints(&feature("my-feature", vec![prop(name, &TypeRef::Boolean)]))
                    .contains(&"NEGATED_BOOLEAN"),
                "{name} should be flagged"
            );
        }

        assert!(!lints(&feature(
            "my-feature",
            vec![prop("sync-enabled", &TypeRef::Boolean)]
        ))
        .contains(&"NEGATED_BOOLEAN"));

        // Only booleans read as double negatives.
        assert!(!lints(&feature(
            "my-feature",
            vec![prop("hide-after", &TypeRef::Int)]
        ))
        .contains(&"NEGATED_BOOLEAN"));
    }

    #[test]
    fn test_common_prefix() {
        let feature = feature(
            "homescreen",
            vec![
                prop("homescreen-enabled", &TypeRef::Boolean),
                prop("homescreen-sections", &TypeRef::String),
            ],
        );
        let mut out = Vec::new();
        check_feature(&feature, &mut out);
        let findings: Vec<_> = out
            .iter()
            .filter(|f| f.lint.name == "COMMON_PREFIX")
            .collect();
        assert_eq!(findings.len(), 2);
        assert!(findings[0].message.contains("rename it to `enabled`"));
    }

    #[test]
    fn test_shared_predicate_prefix_is_not_a_namespace() {
        // Focus names every variable of its `onboarding` feature `is-*`; that is a
        // boolean convention, not a prefix to strip.
        let feature = feature(
            "onboarding",
            vec![
                prop("is-enabled", &TypeRef::Boolean),
                prop("is-cfr-enabled", &TypeRef::Boolean),
            ],
        );
        assert!(!lints(&feature).contains(&"COMMON_PREFIX"));
    }

    #[test]
    fn test_shared_prefix_that_isnt_the_feature_name() {
        let shared = feature(
            "homescreen",
            vec![
                prop("section-order", &TypeRef::String),
                prop("section-titles", &TypeRef::String),
            ],
        );
        assert!(lints(&shared).contains(&"COMMON_PREFIX"));

        // One variable isn't a pattern.
        let alone = feature("homescreen", vec![prop("section-order", &TypeRef::String)]);
        assert!(!lints(&alone).contains(&"COMMON_PREFIX"));
    }
}
