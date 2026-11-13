/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::FMLError;
use crate::intermediate_representation::{FeatureDef, FeatureManifest, TypeFinder, TypeRef};
use crate::{
    error::Result,
    intermediate_representation::{EnumDef, ObjectDef},
};
use regex::Regex;
use std::collections::{BTreeMap, HashSet};

const DISALLOWED_PREFS: &[(&str, &str)] = &[
    (
        r#"^app\.shield\.optoutstudies\.enabled$"#,
        "disabling Nimbus causes immediate unenrollment",
    ),
    (
        r#"^datareporting\.healthreport\.uploadEnabled$"#,
        "disabling telemetry causes immediate unenrollment",
    ),
    (
        r#"^services\.settings\.server$"#,
        "changing the Remote Settings endpoint will break clients",
    ),
    (r#"^nimbus\.debug$"#, "internal Nimbus preference for QA"),
    (
        r#"^security\.turn_off_all_security_so_that_viruses_can_take_over_this_computer$"#,
        "this pref is automation-only and is unsafe to enable outside tests",
    ),
];

pub(crate) struct SchemaValidator<'a> {
    enum_defs: &'a BTreeMap<String, EnumDef>,
    object_defs: &'a BTreeMap<String, ObjectDef>,

    lax_gecko_pref_validation: bool,
}

impl<'a> SchemaValidator<'a> {
    pub(crate) fn new(
        enums: &'a BTreeMap<String, EnumDef>,
        objs: &'a BTreeMap<String, ObjectDef>,
    ) -> Self {
        Self {
            enum_defs: enums,
            object_defs: objs,
            lax_gecko_pref_validation: false,
        }
    }

    pub(crate) fn with_lax_gecko_pref_validation(mut self, value: bool) -> Self {
        self.lax_gecko_pref_validation = value;
        self
    }

    fn _get_enum(&self, nm: &str) -> Option<&EnumDef> {
        self.enum_defs.get(nm)
    }

    fn get_object(&self, nm: &str) -> Option<&ObjectDef> {
        self.object_defs.get(nm)
    }

    pub(crate) fn validate_object_def(&self, object_def: &ObjectDef) -> Result<()> {
        let obj_nm = &object_def.name;
        for prop in &object_def.props {
            let prop_nm = &prop.name;

            // Check the types exist for this property.
            let path = format!("objects/{obj_nm}/{prop_nm}");
            self.validate_type_ref(&path, &prop.typ)?;
        }

        Ok(())
    }

    pub(crate) fn validate_feature_def(&self, feature_def: &FeatureDef) -> Result<()> {
        let feat_nm = &feature_def.name;
        let mut string_aliases: HashSet<_> = Default::default();

        for prop in &feature_def.props {
            let prop_nm = &prop.name;
            let prop_t = &prop.typ;

            let path = format!("features/{feat_nm}/{prop_nm}");

            // Check the types exist for this property.
            self.validate_type_ref(&path, prop_t)?;

            // Check pref is not in the disallowed prefs list.
            if let Some(pref) = &prop.gecko_pref {
                for (pref_str, error) in DISALLOWED_PREFS {
                    let regex = Regex::new(pref_str)?;
                    if regex.is_match(&pref.pref()) {
                        return Err(FMLError::ValidationError(
                            path,
                            format!(
                                "Cannot use pref `{}` in experiments, reason: {}",
                                pref.pref(),
                                error
                            ),
                        ));
                    }
                }
            }

            // Check pref support for this type.
            if prop.gecko_pref.is_some()
                && !prop
                    .typ
                    .supports_gecko_prefs(self.lax_gecko_pref_validation)
            {
                return Err(FMLError::ValidationError(
                    path,
                    "Pref keys can only be used with Option<Boolean>, Option<Int>, and Option<String>  variables"
                        .to_string(),
                ));
            }

            // Check string-alias definition.
            if let Some(sa) = &prop.string_alias {
                // Check that the string-alias has only been defined once in this feature.
                if !string_aliases.insert(sa) {
                    return Err(FMLError::ValidationError(
                        path,
                        format!("The string-alias {sa} should only be declared once per feature"),
                    ));
                }

                // Check that the string-alias is actually used in this property type.
                let types = prop_t.all_types();
                if !types.contains(sa) {
                    return Err(FMLError::ValidationError(
                        path,
                        format!(
                            "The string-alias {sa} must be part of the {} type declaration",
                            prop_nm
                        ),
                    ));
                }
            }
        }

        // Now check that that there is a path from this feature to any objects using the
        // string-aliases defined in this feature.
        let types = feature_def.all_types();
        self.validate_string_alias_declarations(
            &format!("features/{feat_nm}"),
            feat_nm,
            &types,
            &string_aliases,
        )?;

        Ok(())
    }

    pub(crate) fn validate_prefs(&self, feature_manifest: &FeatureManifest) -> Result<()> {
        let prefs = feature_manifest
            .iter_gecko_prefs()
            .map(|p| p.pref())
            .collect::<Vec<String>>();
        for pref in prefs.clone() {
            if prefs
                .iter()
                .map(|p| if p == &pref { 1 } else { 0 })
                .sum::<i32>()
                > 1
            {
                let path = format!(r#"prefs/"{}""#, pref);
                return Err(FMLError::ValidationError(
                    path,
                    "Prefs can only be include once per feature manifest".into(),
                ));
            }
        }

        Ok(())
    }

    fn validate_string_alias_declarations(
        &self,
        path: &str,
        feature: &str,
        types: &HashSet<TypeRef>,
        string_aliases: &HashSet<&TypeRef>,
    ) -> Result<()> {
        let unaccounted: Vec<_> = types
            .iter()
            .filter(|t| matches!(t, TypeRef::StringAlias(_)))
            .filter(|t| !string_aliases.contains(t))
            .collect();

        if !unaccounted.is_empty() {
            let t = unaccounted.first().unwrap();
            return Err(FMLError::ValidationError(
                path.to_string(),
                format!("A string-alias {t} is used by– but has not been defined in– the {feature} feature"),
            ));
        }
        for t in types {
            if let TypeRef::Object(nm) = t {
                if let Some(obj) = self.get_object(nm) {
                    let types = obj.all_types();
                    self.validate_string_alias_declarations(
                        &format!("objects/{nm}"),
                        feature,
                        &types,
                        string_aliases,
                    )?;
                }
            }
        }
        Ok(())
    }

    fn validate_type_ref(&self, path: &str, type_ref: &TypeRef) -> Result<()> {
        match type_ref {
            TypeRef::Enum(name) => {
                if !self.enum_defs.contains_key(name) {
                    return Err(FMLError::ValidationError(
                        path.to_string(),
                        format!("Found enum reference with name: {name}, but no definition"),
                    ));
                }
            }
            TypeRef::Object(name) => {
                if !self.object_defs.contains_key(name) {
                    return Err(FMLError::ValidationError(
                        path.to_string(),
                        format!("Found object reference with name: {name}, but no definition"),
                    ));
                }
            }
            TypeRef::EnumMap(key_type, value_type) => match key_type.as_ref() {
                TypeRef::Enum(_) | TypeRef::String | TypeRef::StringAlias(_) => {
                    self.validate_type_ref(path, key_type)?;
                    self.validate_type_ref(path, value_type)?;
                }
                _ => {
                    return Err(FMLError::ValidationError(
                        path.to_string(),
                        format!(
                            "Map key must be a String, string-alias or enum, found: {key_type:?}",
                        ),
                    ))
                }
            },
            TypeRef::List(list_type) => self.validate_type_ref(path, list_type)?,
            TypeRef::StringMap(value_type) => self.validate_type_ref(path, value_type)?,
            TypeRef::Option(option_type) => {
                if let TypeRef::Option(_) = option_type.as_ref() {
                    return Err(FMLError::ValidationError(
                        path.to_string(),
                        "Found nested optional types".into(),
                    ));
                } else {
                    self.validate_type_ref(path, option_type)?
                }
            }
            _ => (),
        };
        Ok(())
    }
}

#[cfg(test)]
mod manifest_schema {
    use serde_json::json;

    use super::*;
    use crate::error::Result;
    use crate::intermediate_representation::{PrefBranch, PropDef};

    #[test]
    fn validate_enum_type_ref_doesnt_match_def() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop name",
                &TypeRef::Enum("EnumDoesntExist".into()),
                &json!(null),
            )],
            false,
        );
        validator.validate_feature_def(&fm).expect_err(
            "Should fail since EnumDoesntExist isn't a an enum defined in the manifest",
        );
        Ok(())
    }

    #[test]
    fn validate_obj_type_ref_doesnt_match_def() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop name",
                &TypeRef::Object("ObjDoesntExist".into()),
                &json!(null),
            )],
            false,
        );
        validator.validate_feature_def(&fm).expect_err(
            "Should fail since ObjDoesntExist isn't a an Object defined in the manifest",
        );
        Ok(())
    }

    #[test]
    fn validate_enum_map_with_non_enum_key() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop_name",
                &TypeRef::EnumMap(Box::new(TypeRef::Int), Box::new(TypeRef::String)),
                &json!(null),
            )],
            false,
        );
        validator
            .validate_feature_def(&fm)
            .expect_err("Should fail since the key on an EnumMap must be an Enum");
        Ok(())
    }

    #[test]
    fn validate_list_with_enum_with_no_def() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop name",
                &TypeRef::List(Box::new(TypeRef::Enum("EnumDoesntExist".into()))),
                &json!(null),
            )],
            false,
        );
        validator
            .validate_feature_def(&fm)
            .expect_err("Should fail EnumDoesntExist isn't a an enum defined in the manifest");
        Ok(())
    }

    #[test]
    fn validate_enum_map_with_enum_with_no_def() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop name",
                &TypeRef::EnumMap(
                    Box::new(TypeRef::Enum("EnumDoesntExist".into())),
                    Box::new(TypeRef::String),
                ),
                &json!(null),
            )],
            false,
        );
        validator.validate_feature_def(&fm).expect_err(
            "Should fail since EnumDoesntExist isn't a an enum defined in the manifest",
        );
        Ok(())
    }

    #[test]
    fn validate_enum_map_with_obj_value_no_def() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop name",
                &TypeRef::EnumMap(
                    Box::new(TypeRef::String),
                    Box::new(TypeRef::Object("ObjDoesntExist".into())),
                ),
                &json!(null),
            )],
            false,
        );
        validator
            .validate_feature_def(&fm)
            .expect_err("Should fail since ObjDoesntExist isn't an Object defined in the manifest");
        Ok(())
    }

    #[test]
    fn validate_string_map_with_enum_value_no_def() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop name",
                &TypeRef::StringMap(Box::new(TypeRef::Enum("EnumDoesntExist".into()))),
                &json!(null),
            )],
            false,
        );
        validator
            .validate_feature_def(&fm)
            .expect_err("Should fail since ObjDoesntExist isn't an Object defined in the manifest");
        Ok(())
    }

    #[test]
    fn validate_nested_optionals_fail() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new(
                "prop name",
                &TypeRef::Option(Box::new(TypeRef::Option(Box::new(TypeRef::String)))),
                &json!(null),
            )],
            false,
        );
        validator
            .validate_feature_def(&fm)
            .expect_err("Should fail since we can't have nested optionals");
        Ok(())
    }

    #[test]
    fn validate_disallowed_pref_fails() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);
        let fm = FeatureDef::new(
            "some_def",
            "test doc",
            vec![PropDef::new_with_gecko_pref(
                "prop name",
                &TypeRef::String,
                &json!(null),
                "app.shield.optoutstudies.enabled",
                PrefBranch::User,
            )],
            false,
        );
        validator
            .validate_feature_def(&fm)
            .expect_err("Should fail since we can't use that pref for experimentation");
        Ok(())
    }
}

#[cfg(test)]
mod string_aliases {
    use std::slice;

    use serde_json::json;

    use crate::intermediate_representation::PropDef;

    use super::*;

    fn with_objects(objects: &[ObjectDef]) -> BTreeMap<String, ObjectDef> {
        let mut obj_defs: BTreeMap<_, _> = Default::default();
        for o in objects {
            obj_defs.insert(o.name(), o.clone());
        }
        obj_defs
    }

    fn with_feature(props: &[PropDef]) -> FeatureDef {
        FeatureDef::new("test-feature", "", props.into(), false)
    }

    #[test]
    fn test_validate_feature_schema() -> Result<()> {
        let name = TypeRef::StringAlias("PersonName".to_string());
        let all_names = {
            let t = TypeRef::List(Box::new(name.clone()));
            let v = json!(["Alice", "Bonnie", "Charlie", "Denise", "Elise", "Frankie"]);
            PropDef::with_string_alias("all-names", &t, &v, &name)
        };

        let all_names2 = {
            let t = TypeRef::List(Box::new(name.clone()));
            let v = json!(["Alice", "Bonnie"]);
            PropDef::with_string_alias("all-names-duplicate", &t, &v, &name)
        };

        let enums = Default::default();
        let objects = Default::default();
        let validator = SchemaValidator::new(&enums, &objects);

        // -> Verify that only one property per feature can define the same string-alias.
        let fm = with_feature(&[all_names.clone(), all_names2.clone()]);
        assert!(validator.validate_feature_def(&fm).is_err());

        let newest_member = {
            let t = &name;
            let v = json!("Alice"); // it doesn't matter for this test what the value is.
            PropDef::new("newest-member", t, &v)
        };

        // -> Verify that a property in a feature can validate against the a string-alias
        // -> in the same feature.
        // { all-names: ["Alice"], newest-member: "Alice" }
        let fm = with_feature(&[all_names.clone(), newest_member.clone()]);
        validator.validate_feature_def(&fm)?;

        // { newest-member: "Alice" }
        // We have a reference to a team mate, but no definitions.
        // Should error out.
        let fm = with_feature(slice::from_ref(&newest_member));
        assert!(validator.validate_feature_def(&fm).is_err());

        // -> Validate a property in a nested object can validate against a string-alias
        // -> in a feature that uses the object.
        let team_def = ObjectDef::new("Team", slice::from_ref(&newest_member));
        let team = {
            let t = TypeRef::Object("Team".to_string());
            let v = json!({ "newest-member": "Alice" });

            PropDef::new("team", &t, &v)
        };

        // { all-names: ["Alice"], team: { newest-member: "Alice" } }
        let fm = with_feature(&[all_names.clone(), team.clone()]);
        let objs = with_objects(slice::from_ref(&team_def));
        let validator = SchemaValidator::new(&enums, &objs);
        validator.validate_feature_def(&fm)?;

        // { team: { newest-member: "Alice" } }
        let fm = with_feature(slice::from_ref(&team));
        let objs = with_objects(slice::from_ref(&team_def));
        let validator = SchemaValidator::new(&enums, &objs);
        assert!(validator.validate_feature_def(&fm).is_err());

        // -> Validate a property in a deeply nested object can validate against a string-alias
        // -> in a feature that uses the object.

        let match_def = ObjectDef::new("Match", slice::from_ref(&team));
        let match_ = {
            let t = TypeRef::Object("Match".to_string());
            let v = json!({ "team": { "newest-member": "Alice" }});

            PropDef::new("match", &t, &v)
        };

        // { all-names: ["Alice"], match: { team: { newest-member: "Alice" }} }
        let fm = with_feature(&[all_names.clone(), match_.clone()]);
        let objs = with_objects(&[team_def.clone(), match_def.clone()]);
        let validator = SchemaValidator::new(&enums, &objs);
        validator.validate_feature_def(&fm)?;

        // { match: {team: { newest-member: "Alice" }} }
        let fm = with_feature(slice::from_ref(&match_));
        let validator = SchemaValidator::new(&enums, &objs);
        assert!(validator.validate_feature_def(&fm).is_err());

        Ok(())
    }
}

#[cfg(test)]
mod test_gecko_prefs {
    use serde_json::json;

    use crate::error::FMLError;
    use crate::intermediate_representation::{GeckoPrefDef, PrefBranch, PropDef};

    use super::*;

    #[test]
    fn test_validate_gecko_pref_supported_types() {
        let mut prop = PropDef::new("var", &TypeRef::String, &json!(null));
        prop.gecko_pref = Some(GeckoPrefDef {
            pref: "some.pref".into(),
            branch: PrefBranch::User,
        });

        let feature = FeatureDef {
            name: "Feature".to_string(),
            props: vec![prop],
            ..Default::default()
        };

        let objs = Default::default();
        let enums = Default::default();
        let validator = SchemaValidator::new(&enums, &objs);

        assert!(matches!(
            validator.validate_feature_def(&feature),
            Err(FMLError::ValidationError(path, reason)) if path == "features/Feature/var" && reason == "Pref keys can only be used with Option<Boolean>, Option<Int>, and Option<String>  variables"
        ));

        let validator = SchemaValidator::new(&enums, &objs).with_lax_gecko_pref_validation(true);

        assert!(matches!(validator.validate_feature_def(&feature), Ok(())));
    }
}
