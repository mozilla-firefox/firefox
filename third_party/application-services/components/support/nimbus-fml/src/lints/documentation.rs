/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Lints about descriptions, which are what an experiment owner reads in
//! Experimenter when deciding what to put in a branch.

use super::{
    enum_path, enum_variant_path, feature_path, object_field_path, object_path, variable_path,
    LintInfo, Linter, Location, RawFinding,
};
use crate::intermediate_representation::{EnumDef, FeatureDef, FeatureManifest, ObjectDef};

define_lints! {
    MISSING_DESCRIPTION: Documentation, Warning =
        "Everything in a manifest should have a description.",
        "Descriptions are shown to experiment owners in Experimenter: say what the thing is for, and what changes when it changes.";
    TERSE_DESCRIPTION: Documentation, Warning =
        "Descriptions should say more than the name already does.",
        "The name is already visible next to the description; use the description to say what changes when the value changes.";
    TODO_IN_DESCRIPTION: Documentation, Warning =
        "Descriptions shouldn't be left as placeholders.",
        "Placeholder descriptions ship to Experimenter as-is.";
}

/// Shorter than this is almost certainly a restatement of the name.
const MINIMUM_WORDS: usize = 3;

const PLACEHOLDERS: &[&str] = &["todo", "fixme", "tbd", "xxx", "wip"];

pub(crate) struct Documentation;

impl Linter for Documentation {
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
    check_description(
        "feature",
        &feature.name,
        &feature.metadata.description,
        feature_path(feature),
        out,
    );
    for prop in &feature.props {
        check_description(
            "variable",
            &prop.name,
            &prop.doc,
            variable_path(feature, prop),
            out,
        );
    }
}

fn check_object(object: &ObjectDef, out: &mut Vec<RawFinding>) {
    check_description(
        "object",
        &object.name,
        &object.doc,
        object_path(object),
        out,
    );
    for prop in &object.props {
        check_description(
            "field",
            &prop.name,
            &prop.doc,
            object_field_path(object, prop),
            out,
        );
    }
}

fn check_enum(enum_def: &EnumDef, out: &mut Vec<RawFinding>) {
    check_description(
        "enum",
        &enum_def.name,
        &enum_def.doc,
        enum_path(enum_def),
        out,
    );
    for variant in &enum_def.variants {
        check_description(
            "variant",
            &variant.name,
            &variant.doc,
            enum_variant_path(enum_def, &variant.name),
            out,
        );
    }
}

fn check_description(
    what: &str,
    name: &str,
    description: &str,
    path: Location,
    out: &mut Vec<RawFinding>,
) {
    let description = description.trim();

    // Findings are grouped under their feature, object or enum, so only members
    // have to name themselves.
    let subject = if path.is_member() {
        format!("`{name}`")
    } else {
        format!("this {what}")
    };

    if description.is_empty() {
        out.push(RawFinding::new(
            &MISSING_DESCRIPTION,
            path,
            format!("{subject} has no description"),
        ));
        return;
    }

    if let Some(placeholder) = placeholder_in(description) {
        out.push(RawFinding::new(
            &TODO_IN_DESCRIPTION,
            path.clone(),
            format!("The description of {subject} is still marked `{placeholder}`"),
        ));
    }

    let words = description.split_whitespace().count();
    if words < MINIMUM_WORDS {
        out.push(RawFinding::new(
            &TERSE_DESCRIPTION,
            path,
            format!(
                "The description of {subject} is only {words} word{}: `{description}`",
                if words == 1 { "" } else { "s" }
            ),
        ));
    } else if restates_name(name, description) {
        out.push(RawFinding::new(
            &TERSE_DESCRIPTION,
            path,
            format!("The description of {subject} just restates its name"),
        ));
    }
}

fn placeholder_in(description: &str) -> Option<&'static str> {
    let words: Vec<String> = description
        .split(|c: char| !c.is_alphanumeric())
        .map(str::to_ascii_lowercase)
        .collect();
    PLACEHOLDERS
        .iter()
        .find(|p| words.iter().any(|w| w == *p))
        .copied()
}

fn restates_name(name: &str, description: &str) -> bool {
    normalize(name) == normalize(description)
}

fn normalize(value: &str) -> String {
    let words: Vec<String> = value
        .split(|c: char| !c.is_alphanumeric())
        .filter(|s| !s.is_empty())
        .map(str::to_ascii_lowercase)
        .collect();

    let words = match words.split_first() {
        Some((first, rest)) if ["a", "an", "the"].contains(&first.as_str()) => rest,
        _ => &words,
    };

    words.join(" ")
}

#[cfg(test)]
mod unit_tests {
    use super::*;
    use crate::intermediate_representation::{FeatureDef, PropDef, TypeRef};
    use serde_json::json;

    fn test_location() -> Location {
        feature_path(&FeatureDef::new("a-feature", "", Default::default(), false))
    }

    fn lints(name: &str, description: &str) -> Vec<&'static str> {
        let mut out = Vec::new();
        check_description("variable", name, description, test_location(), &mut out);
        out.iter().map(|f| f.lint.name).collect()
    }

    #[test]
    fn test_missing_description() {
        assert_eq!(lints("my-variable", ""), vec!["MISSING_DESCRIPTION"]);
        assert_eq!(lints("my-variable", "   "), vec!["MISSING_DESCRIPTION"]);
    }

    #[test]
    fn test_good_description() {
        assert!(lints(
            "max-rows",
            "The largest number of rows the widget can grow to."
        )
        .is_empty());
    }

    #[test]
    fn test_terse_description() {
        assert_eq!(lints("enabled", "Enabled"), vec!["TERSE_DESCRIPTION"]);
        assert_eq!(
            lints("section-order", "The section order"),
            vec!["TERSE_DESCRIPTION"]
        );
    }

    #[test]
    fn test_description_restating_the_name() {
        assert_eq!(
            lints("max-visible-rows", "Max visible rows"),
            vec!["TERSE_DESCRIPTION"]
        );
        assert_eq!(
            lints("max-visible-rows", "max_visible_rows."),
            vec!["TERSE_DESCRIPTION"]
        );
        assert!(lints("max-visible-rows", "How many rows are visible at once.").is_empty());
    }

    #[test]
    fn test_objects_and_enums_and_their_members() {
        let mut object = ObjectDef::new(
            "Section",
            &[PropDef::with_doc(
                "title",
                "",
                &TypeRef::String,
                &json!(null),
            )],
        );
        object.doc = String::new();

        let mut enum_def = EnumDef::new("Style", &["outline"]);
        enum_def.doc = String::new();
        enum_def.variants[0].doc = String::new();

        let mut out = Vec::new();
        check_object(&object, &mut out);
        check_enum(&enum_def, &mut out);

        // Members name themselves; the object or enum they're under doesn't have to.
        let messages: Vec<_> = out.iter().map(|f| f.message.as_str()).collect();
        assert_eq!(
            messages,
            vec![
                "this object has no description",
                "`title` has no description",
                "this enum has no description",
                "`outline` has no description",
            ]
        );
    }

    #[test]
    fn test_placeholder_description() {
        assert_eq!(
            lints("my-variable", "TODO: write this description"),
            vec!["TODO_IN_DESCRIPTION"]
        );
        assert_eq!(
            lints(
                "my-variable",
                "The colour of the button (FIXME: which one?)"
            ),
            vec!["TODO_IN_DESCRIPTION"]
        );
        // `todos` is a word, not a placeholder.
        assert!(lints("my-variable", "The list of todos shown in the widget.").is_empty());
    }
}
