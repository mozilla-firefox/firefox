/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::editing::{ErrorConverter, ErrorKind, ErrorPath, FeatureValidationError};
use crate::error::FMLError;
use crate::intermediate_representation::{FeatureDef, PropDef, TypeRef};
use crate::{
    error::Result,
    intermediate_representation::{EnumDef, ObjectDef},
};
use serde_json::{Map, Value};
use std::collections::{BTreeMap, HashMap, HashSet};

pub(crate) struct DefaultsValidator<'a> {
    enum_defs: &'a BTreeMap<String, EnumDef>,
    object_defs: &'a BTreeMap<String, ObjectDef>,

    lax_gecko_pref_validation: bool,
}

impl<'a> DefaultsValidator<'a> {
    pub(crate) fn new(
        enum_defs: &'a BTreeMap<String, EnumDef>,
        object_defs: &'a BTreeMap<String, ObjectDef>,
    ) -> Self {
        Self {
            enum_defs,
            object_defs,
            lax_gecko_pref_validation: false,
        }
    }

    pub(crate) fn with_lax_gecko_pref_validation(mut self, value: bool) -> Self {
        self.lax_gecko_pref_validation = value;
        self
    }

    pub(crate) fn validate_object_def(&self, object_def: &ObjectDef) -> Result<(), FMLError> {
        let mut errors = Default::default();
        let path = ErrorPath::object(&object_def.name);
        for prop in &object_def.props {
            self.validate_types(
                &path.property(&prop.name),
                &prop.typ,
                &prop.default,
                &mut errors,
            );
        }
        if errors.is_empty() {
            Ok(())
        } else {
            let converter = ErrorConverter::new(self.enum_defs, self.object_defs);
            Err(converter.convert_object_error(errors.pop().unwrap()))
        }
    }

    /// This is called as part of the _manifest_ validation only, as part of `fm.validate_defaults()`,
    /// shortly after `fm.validate_schema()`.
    ///
    /// It is not called as part of feature validation, i.e. once the manifest has been loaded
    /// and validated, and now to be used to validate arbitrary JSON.
    ///
    /// It bails with the first detected error. The error detection itself occurs with
    /// the `get_errors` call below.
    ///
    /// It does not check if there are spurious keys in a feature than are defined (this is done in the DefaultsMerger).
    /// It does check if the features enum maps have a complete set of variants as keys.
    ///
    pub(crate) fn validate_feature_def(&self, feature_def: &FeatureDef) -> Result<()> {
        let defaults = feature_def.default_json();
        let errors = self.get_errors(feature_def, &defaults, &defaults);
        self.guard_errors(feature_def, &defaults, errors)?;

        // This is only checking if a Map with an Enum as key has a complete set of keys (i.e. all variants)
        self.validate_feature_enum_maps(feature_def)?;

        if !self.lax_gecko_pref_validation {
            self.validate_no_defaults_for_gecko_prefs(feature_def)?;
        }

        // Now check the examples for this feature.
        let path = ErrorPath::feature(&feature_def.name);
        for ex in &feature_def.examples {
            let path = path.example(&ex.metadata.name);
            let errors = self.get_errors_with_path(&path, feature_def, &defaults, &ex.value);
            self.guard_errors(feature_def, &defaults, errors)?;
        }

        Ok(())
    }

    pub(crate) fn guard_errors(
        &self,
        feature_def: &FeatureDef,
        defaults: &Value,
        mut errors: Vec<FeatureValidationError>,
    ) -> Result<()> {
        if !errors.is_empty() {
            let converter = ErrorConverter::new(self.enum_defs, self.object_defs);
            Err(converter.convert_feature_error(feature_def, defaults, errors.pop().unwrap()))
        } else {
            Ok(())
        }
    }

    /// Called as part of validating any feature def against a full JSON defaults (either the default json, or something
    /// merged on to a default json).
    pub(crate) fn get_errors(
        &self,
        feature_def: &FeatureDef,
        merged_value: &Value,
        unmerged_value: &Value,
    ) -> Vec<FeatureValidationError> {
        let path = ErrorPath::feature(&feature_def.name);
        self.get_errors_with_path(&path, feature_def, merged_value, unmerged_value)
    }

    pub(crate) fn get_errors_with_path(
        &self,
        path: &ErrorPath,
        feature_def: &FeatureDef,
        merged_value: &Value,
        unmerged_value: &Value,
    ) -> Vec<FeatureValidationError> {
        let mut errors = Default::default();
        let unmerged_map = unmerged_value
            .as_object()
            .expect("Assumption: an object is the only type that can get here");
        self.validate_props_types(path, &feature_def.props, unmerged_map, &mut errors);
        if !errors.is_empty() {
            return errors;
        }

        let string_aliases = feature_def.get_string_aliases();
        for prop in &feature_def.props {
            if let Some(value) = unmerged_map.get(&prop.name) {
                self.validate_string_aliases(
                    &path.property(&prop.name),
                    &prop.typ,
                    value,
                    &string_aliases,
                    merged_value,
                    &prop.string_alias,
                    &mut errors,
                );
            }
        }
        errors
    }

    fn get_enum(&self, nm: &str) -> Option<&EnumDef> {
        self.enum_defs.get(nm)
    }

    fn get_object(&self, nm: &str) -> Option<&ObjectDef> {
        self.object_defs.get(nm)
    }

    fn validate_feature_enum_maps(&self, feature_def: &FeatureDef) -> Result<()> {
        let path = ErrorPath::feature(&feature_def.name);
        for prop in &feature_def.props {
            let path = path.property(&prop.name);
            self.validate_enum_maps(&path, &prop.typ, &prop.default)?;
        }
        Ok(())
    }

    fn validate_no_defaults_for_gecko_prefs(&self, feature_def: &FeatureDef) -> Result<()> {
        let path = ErrorPath::feature(&feature_def.name);
        for prop in &feature_def.props {
            if prop.gecko_pref.is_some() && !prop.default.is_null() {
                let path = path.property(&prop.name);
                return Err(FMLError::ValidationError(
                    path.path,
                    "gecko-pref and default are mutually exclusive".into(),
                ));
            }
        }
        Ok(())
    }

    /// Check enum maps (Map<Enum, T>) have all keys represented.
    ///
    /// We split this out because if the FML has all keys, then any feature configs do as well.
    ///
    /// Thus, we don't need to do the detection when editing a feature config.
    fn validate_enum_maps(
        &self,
        path: &ErrorPath,
        type_ref: &TypeRef,
        default: &Value,
    ) -> Result<()> {
        match (type_ref, default) {
            (TypeRef::Option(inner), v) => {
                self.validate_enum_maps(path, inner, v)?
            }

            (TypeRef::EnumMap(enum_type, map_type), Value::Object(map))
                if matches!(**enum_type, TypeRef::Enum(_)) =>
            {
                let enum_name = enum_type.name().unwrap();
                let enum_def = self
                    .get_enum(enum_name)
                    // If this is thrown, there's a problem in validate_type_ref.
                    .unwrap_or_else(|| {
                        unreachable!("Enum {enum_name} is not defined in the manifest")
                    });

                let mut unseen = HashSet::new();
                if !matches!(**map_type, TypeRef::Option(_)) {
                    for variant in &enum_def.variants {
                        if !map.contains_key(&variant.name) {
                            unseen.insert(variant.name());
                        }
                    }

                    if !unseen.is_empty() {
                        let path = path.open_brace();
                        return Err(FMLError::ValidationError(
                            path.path,
                            format!("Enum map {enum_name} is missing values for {unseen:?}"),
                        ));
                    }
                }

                for (key, value) in map {
                    self.validate_enum_maps(&path.enum_map_key(enum_name, key), map_type, value)?
                }
            }

            (TypeRef::EnumMap(_, map_type), Value::Object(map)) // Map<string-alias, T>
            | (TypeRef::StringMap(map_type), Value::Object(map)) => {
                for (key, value) in map {
                    self.validate_enum_maps(&path.map_key(key), map_type, value)?
                }
            }

            (TypeRef::List(list_type), Value::Array(arr)) => {
                for (index, value) in arr.iter().enumerate() {
                    self.validate_enum_maps(&path.array_index(index), list_type, value)?
                }
            }

            (TypeRef::Object(obj_name), Value::Object(map)) => {
                let obj_def = self
                    .get_object(obj_name)
                    // If this is thrown, there's a problem in validate_type_ref.
                    .unwrap_or_else(|| {
                        unreachable!("Object {obj_name} is not defined in the manifest")
                    });
                let path = path.object_value(obj_name);
                for prop in &obj_def.props {
                    if let Some(value) = map.get(&prop.name) {
                        self.validate_enum_maps(&path.property(&prop.name), &prop.typ, value)?
                    }
                }
            }

            _ => (),
        };
        Ok(())
    }

    fn validate_types(
        &self,
        path: &ErrorPath,
        type_ref: &TypeRef,
        default: &Value,
        errors: &mut Vec<FeatureValidationError>,
    ) {
        match (type_ref, default) {
            (TypeRef::Boolean, Value::Bool(_))
            | (TypeRef::BundleImage, Value::String(_))
            | (TypeRef::BundleText, Value::String(_))
            | (TypeRef::String, Value::String(_))
            | (TypeRef::StringAlias(_), Value::String(_))
            | (TypeRef::Int, Value::Number(_))
            | (TypeRef::Option(_), Value::Null) => (),
            (TypeRef::Option(inner), v) => {
                self.validate_types(path, inner, v, errors)
            }
            (TypeRef::Enum(enum_name), Value::String(s)) => {
                let enum_def = self
                    .get_enum(enum_name)
                    // If this is thrown, there's a problem in validate_type_ref.
                    .unwrap_or_else(|| {
                        unreachable!("Enum {enum_name} is not defined in the manifest")
                    });
                let mut valid = HashSet::new();
                for variant in enum_def.variants() {
                    let name = variant.name();
                    if *s == name {
                        return;
                    }
                    valid.insert(name);
                }
                let path = path.final_error_quoted(s);
                errors.push(FeatureValidationError {
                    path,
                    kind: ErrorKind::invalid_value(type_ref),
                });
            }
            (TypeRef::EnumMap(enum_type, map_type), Value::Object(map))
                if matches!(**enum_type, TypeRef::Enum(_)) =>
            {
                let enum_name = enum_type.name().unwrap();
                let enum_def = self
                    .get_enum(enum_name)
                    // If this is thrown, there's a problem in validate_type_ref.
                    .unwrap_or_else(|| {
                        unreachable!("Enum {enum_name} is not defined in the manifest")
                    });

                // We first validate that the keys of the map cover all all the enum variants, and no more or less
                let mut valid = HashSet::new();
                for variant in &enum_def.variants {
                    let nm = &variant.name;
                    valid.insert(nm.clone());

                    let map_value = map.get(nm);
                    match (map_type.as_ref(), map_value) {
                        (TypeRef::Option(_), None) => (),
                        (_, Some(inner)) => {
                            self.validate_types(&path.enum_map_key(enum_name, nm), map_type, inner, errors);
                        }
                        _ => ()
                    }
                }

                for (map_key, map_value) in map {
                    if !valid.contains(map_key) {
                        let path = path.map_key(map_key);
                        errors.push(FeatureValidationError {
                            path,
                            kind: ErrorKind::invalid_key(enum_type, map),
                        });
                    }

                    self.validate_types(&path.enum_map_key(&enum_def.name, map_key), map_type, map_value, errors);
                }
            }
            (TypeRef::EnumMap(_, map_type), Value::Object(map)) // Map<string-alias, T>
            | (TypeRef::StringMap(map_type), Value::Object(map)) => {
                for (key, value) in map {
                    self.validate_types(&path.map_key(key), map_type, value, errors);
                }
            }
            (TypeRef::List(list_type), Value::Array(arr)) => {
                for (index, value) in arr.iter().enumerate() {
                    self.validate_types(&path.array_index(index), list_type, value, errors);
                }
            }
            (TypeRef::Object(obj_name), Value::Object(map)) => {
                let obj_def = self
                    .get_object(obj_name)
                    // If this is thrown, there's a problem in validate_type_ref.
                    .unwrap_or_else(|| {
                        unreachable!("Object {obj_name} is not defined in the manifest")
                    });
                self.validate_props_types(&path.object_value(obj_name), &obj_def.props, map, errors);
            }
            _ => {
                let path = path.final_error_value(default);
                errors.push(FeatureValidationError {
                    path,
                    kind: ErrorKind::type_mismatch(type_ref),
                });
            }
        };
    }

    fn validate_props_types(
        &self,
        path: &ErrorPath,
        props: &Vec<PropDef>,
        map: &Map<String, Value>,
        errors: &mut Vec<FeatureValidationError>,
    ) {
        let mut valid = HashSet::new();

        for prop in props {
            // We only check the defaults overriding the property defaults
            // from the object's own property defaults.
            // We check the object property defaults previously.
            let prop_name = &prop.name;
            if let Some(map_val) = map.get(prop_name) {
                self.validate_types(&path.property(prop_name), &prop.typ, map_val, errors);
            }

            valid.insert(prop_name.clone());
        }
        for map_key in map.keys() {
            if !valid.contains(map_key) {
                let path = path.final_error_quoted(map_key);
                errors.push(FeatureValidationError {
                    path,
                    kind: ErrorKind::invalid_prop(props, map),
                });
            }
        }
    }

    /// Validate a property against the string aliases in the feature.
    ///
    /// A property can be of any type: this will recurse into the structural types and object types
    /// looking for strings to validate.
    ///
    /// - path The error path at which to report any errors
    /// - typ The type of the value we're validating. Only objects, structural types and string-aliases will do anything.
    ///   We'll be recursing into this type.
    /// - value The value we're validating. We'll be recursing into this value.
    /// - definitions The properties in this feature that define the string-alias types.
    /// - feature_value The merged value for the entire feature
    /// - skip The property we're validating may include a definition
    #[allow(clippy::too_many_arguments)]
    fn validate_string_aliases(
        &self,
        path: &ErrorPath,
        typ: &TypeRef,
        value: &Value,
        definitions: &HashMap<&str, &PropDef>,
        feature_value: &Value,
        skip: &Option<TypeRef>,
        errors: &mut Vec<FeatureValidationError>,
    ) {
        // As an optimization (to stop validating the definition against itself),
        // we want to skip validation on the `skip` type ref: this is only set by the property defining
        // a string-alias.
        let should_validate = |v: &TypeRef| -> bool { skip.as_ref() != Some(v) };
        match (typ, value) {
            (TypeRef::StringAlias(_), Value::String(s)) => {
                if !is_string_alias_value_valid(typ, s, definitions, feature_value) {
                    let path = path.final_error_quoted(s);
                    errors.push(FeatureValidationError {
                        path,
                        kind: ErrorKind::invalid_value(typ),
                    });
                }
            }
            (TypeRef::Option(_), &Value::Null) => (),
            (TypeRef::Option(inner), _) => self.validate_string_aliases(
                path,
                inner,
                value,
                definitions,
                feature_value,
                skip,
                errors,
            ),
            (TypeRef::List(inner), Value::Array(array)) => {
                if should_validate(inner) {
                    for (index, value) in array.iter().enumerate() {
                        self.validate_string_aliases(
                            &path.array_index(index),
                            inner,
                            value,
                            definitions,
                            feature_value,
                            skip,
                            errors,
                        );
                    }
                }
            }
            (TypeRef::EnumMap(key_type, value_type), Value::Object(map)) => {
                if should_validate(key_type) && matches!(**key_type, TypeRef::StringAlias(_)) {
                    for key in map.keys() {
                        if !is_string_alias_value_valid(key_type, key, definitions, feature_value) {
                            let path = path.final_error_quoted(key);
                            errors.push(FeatureValidationError {
                                path,
                                kind: ErrorKind::invalid_key(key_type, map),
                            });
                        }
                    }
                }

                if should_validate(value_type) {
                    for (key, value) in map {
                        self.validate_string_aliases(
                            &path.map_key(key),
                            value_type,
                            value,
                            definitions,
                            feature_value,
                            skip,
                            errors,
                        );
                    }
                }
            }
            (TypeRef::StringMap(vt), Value::Object(map)) => {
                if should_validate(vt) {
                    for (key, value) in map {
                        self.validate_string_aliases(
                            &path.map_key(key),
                            vt,
                            value,
                            definitions,
                            feature_value,
                            skip,
                            errors,
                        );
                    }
                }
            }
            (TypeRef::Object(obj_nm), Value::Object(map)) => {
                let path = path.object_value(obj_nm);
                let obj_def = self.get_object(obj_nm).unwrap();

                for prop in &obj_def.props {
                    let prop_nm = &prop.name;
                    if let Some(value) = map.get(prop_nm) {
                        // string-alias definitions aren't allowed in Object definitions,
                        // so `skip` is None.
                        self.validate_string_aliases(
                            &path.property(prop_nm),
                            &prop.typ,
                            value,
                            definitions,
                            feature_value,
                            &None,
                            errors,
                        );
                    } else {
                        // There is no value in the map, so we need to validate the
                        // default.
                        let mut suberrors = Default::default();
                        self.validate_string_aliases(
                            &ErrorPath::object(obj_nm),
                            &prop.typ,
                            &prop.default,
                            definitions,
                            feature_value,
                            &None,
                            &mut suberrors,
                        );

                        // If the default is invalid, then it doesn't really matter
                        // what the error is, we can just error out.
                        if !suberrors.is_empty() {
                            let path = path.open_brace();
                            errors.push(FeatureValidationError {
                                path,
                                kind: ErrorKind::invalid_nested_value(prop_nm, &prop.typ),
                            });
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

fn is_string_alias_value_valid(
    alias_type: &TypeRef,
    value: &str,
    definitions: &HashMap<&str, &PropDef>,
    merged_value: &Value,
) -> bool {
    let alias_name = alias_type
        .name()
        .expect("Assumption: this is a StringAlias type, and it has a name");
    // SchemaValidator checked that the property definitely exists.
    let prop = definitions
        .get(alias_name)
        .expect("Assumption: prop is defined by this feature");
    let prop_value = merged_value
        .get(&prop.name)
        .expect("Assumption: value is defined in this feature");
    validate_string_alias_value(value, alias_type, &prop.typ, prop_value)
}

/// Takes
/// - a string value e.g. "Alice"
/// - a string-alias type, StringAlias("TeamMateName") / TeamMateName
/// - a type definition of a wider collection of teammates: e.g. List<TeamMateName>
/// - an a value for the collection of teammates: e.g. ["Alice", "Bonnie", "Charlie", "Dawn"]
///
/// Given the args, returns a boolean: is the string value in the collection?
///
/// This should work with arbitrary collection types, e.g.
/// - TeamMate,
/// - Option<TeamMate>,
/// - List<TeamMate>,
/// - Map<TeamMate, _>
/// - Map<_, TeamMate>
///
/// and any arbitrary nesting of the collection types.
fn validate_string_alias_value(
    value: &str,
    alias_type: &TypeRef,
    def_type: &TypeRef,
    def_value: &Value,
) -> bool {
    match (def_type, def_value) {
        (TypeRef::StringAlias(_), Value::String(s)) if alias_type == def_type => value == s,

        (TypeRef::Option(dt), dv) if dv != &Value::Null => {
            validate_string_alias_value(value, alias_type, dt, dv)
        }
        (TypeRef::EnumMap(kt, _), Value::Object(map)) if alias_type == &**kt => {
            map.contains_key(value)
        }
        (TypeRef::EnumMap(_, vt), Value::Object(map))
        | (TypeRef::StringMap(vt), Value::Object(map)) => {
            let mut found = false;
            for item in map.values() {
                if validate_string_alias_value(value, alias_type, vt, item) {
                    found = true;
                    break;
                }
            }
            found
        }
        (TypeRef::List(k), Value::Array(array)) => {
            let mut found = false;
            for item in array {
                if validate_string_alias_value(value, alias_type, k, item) {
                    found = true;
                    break;
                }
            }
            found
        }

        _ => false,
    }
}

#[cfg(test)]
mod test_types {

    use serde_json::json;

    use crate::{error::FMLError, intermediate_representation::PropDef};

    use super::*;

    impl DefaultsValidator<'_> {
        fn validate_prop_defaults(&self, prop: &PropDef) -> Result<()> {
            let mut errors = Default::default();
            let path = ErrorPath::feature("test");
            self.validate_types(&path, &prop.typ, &prop.default, &mut errors);
            if let Some(err) = errors.pop() {
                return Err(FMLError::ValidationError(
                    err.path.path,
                    "Error".to_string(),
                ));
            }
            self.validate_enum_maps(&path, &prop.typ, &prop.default)
        }
    }

    fn enums() -> BTreeMap<String, EnumDef> {
        let enum_ = EnumDef::new("ButtonColor", &["blue", "green"]);

        EnumDef::into_map(&[enum_])
    }

    fn objects() -> BTreeMap<String, ObjectDef> {
        let obj1 = ObjectDef::new(
            "SampleObj",
            &[
                PropDef::new("int", &TypeRef::Int, &json!(1)),
                PropDef::new("string", &TypeRef::String, &json!("a string")),
                PropDef::new("enum", &TypeRef::Enum("ButtonColor".into()), &json!("blue")),
                PropDef::new(
                    "list",
                    &TypeRef::List(Box::new(TypeRef::Boolean)),
                    &json!([true, false]),
                ),
                PropDef::new(
                    "optional",
                    &TypeRef::Option(Box::new(TypeRef::Int)),
                    &json!(null),
                ),
                PropDef::new(
                    "nestedObj",
                    &TypeRef::Object("NestedObject".into()),
                    &json!({
                        "enumMap": {
                            "blue": 1,
                        },
                    }),
                ),
            ],
        );

        let obj2 = ObjectDef::new(
            "NestedObject",
            &[PropDef::new(
                "enumMap",
                &TypeRef::EnumMap(
                    Box::new(TypeRef::Enum("ButtonColor".into())),
                    Box::new(TypeRef::Int),
                ),
                &json!({
                    "blue": 4,
                    "green": 2,
                }),
            )],
        );
        ObjectDef::into_map(&[obj1, obj2])
    }

    #[test]
    fn test_validate_prop_defaults_string() -> Result<()> {
        let mut prop = PropDef::new("key", &TypeRef::String, &json!("default!"));
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;

        prop.default = json!(100);
        fm.validate_prop_defaults(&prop)
            .expect_err("Should error out, default is number when it should be string");
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_int() -> Result<()> {
        let mut prop = PropDef::new("key", &TypeRef::Int, &json!(100));
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!("100");

        fm.validate_prop_defaults(&prop)
            .expect_err("Should error out, default is string when it should be number");
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_bool() -> Result<()> {
        let mut prop = PropDef::new("key", &TypeRef::Boolean, &json!(true));
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!("100");

        fm.validate_prop_defaults(&prop)
            .expect_err("Should error out, default is string when it should be a boolean");
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_bundle_image() -> Result<()> {
        let mut prop = PropDef::new("key", &TypeRef::BundleImage, &json!("IconBlue"));
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!(100);

        fm.validate_prop_defaults(&prop).expect_err(
            "Should error out, default is number when it should be a string (bundleImage string)",
        );
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_bundle_text() -> Result<()> {
        let mut prop = PropDef::new("key", &TypeRef::BundleText, &json!("BundledText"));
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!(100);

        fm.validate_prop_defaults(&prop).expect_err(
            "Should error out, default is number when it should be a string (bundleText string)",
        );
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_option_null() -> Result<()> {
        let mut prop = PropDef::new(
            "key",
            &TypeRef::Option(Box::new(TypeRef::Boolean)),
            &json!(null),
        );
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!(100);

        fm.validate_prop_defaults(&prop).expect_err(
            "Should error out, default is number when it should be a boolean (Optional boolean)",
        );
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_option_non_null() -> Result<()> {
        let mut prop = PropDef::new(
            "key",
            &TypeRef::Option(Box::new(TypeRef::Boolean)),
            &json!(true),
        );
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;

        prop.default = json!(100);
        fm.validate_prop_defaults(&prop).expect_err(
            "Should error out, default is number when it should be a boolean (Optional boolean)",
        );
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_enum() -> Result<()> {
        let mut prop = PropDef::new("key", &TypeRef::Enum("ButtonColor".into()), &json!("blue"));

        let enums1 = enums();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!("green");

        fm.validate_prop_defaults(&prop)?;
        prop.default = json!("not a valid color");

        fm.validate_prop_defaults(&prop)
            .expect_err("Should error out since default is not a valid enum variant");
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_enum_map() -> Result<()> {
        let mut prop = PropDef::new(
            "key",
            &TypeRef::EnumMap(
                Box::new(TypeRef::Enum("ButtonColor".into())),
                Box::new(TypeRef::Int),
            ),
            &json!({
                "blue": 1,
                "green": 22,
            }),
        );
        let enums1 = enums();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!({
            "blue": 1,
        });
        fm.validate_prop_defaults(&prop)
            .expect_err("Should error out because the enum map is missing the green key");

        prop.default = json!({
            "blue": 1,
            "green": 22,
            "red": 3,
        });
        fm.validate_prop_defaults(&prop).expect_err("Should error out because the default includes an extra key that is not a variant of the enum (red)");
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_string_map() -> Result<()> {
        let mut prop = PropDef::new(
            "key",
            &TypeRef::StringMap(Box::new(TypeRef::Int)),
            &json!({
                "blue": 1,
                "green": 22,
            }),
        );
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;
        prop.default = json!({
            "blue": 1,
        });
        fm.validate_prop_defaults(&prop)?;

        prop.default = json!({
            "blue": 1,
            "green": 22,
            "red": 3,
            "white": "AHA not a number"
        });
        fm.validate_prop_defaults(&prop).expect_err("Should error out because the string map includes a value that is not an int as defined by the TypeRef");
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_list() -> Result<()> {
        let mut prop = PropDef::new(
            "key",
            &TypeRef::List(Box::new(TypeRef::Int)),
            &json!([1, 3, 100]),
        );
        let enums1 = Default::default();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;

        prop.default = json!([1, 2, "oops"]);
        fm.validate_prop_defaults(&prop)
            .expect_err("Should error out because one of the values in the array is not an int");
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_object() -> Result<()> {
        let mut prop = PropDef::new(
            "key",
            &TypeRef::Object("SampleObj".into()),
            &json!({
                "int": 1,
                "string": "bobo",
                "enum": "green",
                "list": [true, false, true],
                "nestedObj": {
                    "enumMap": {
                        "blue": 1,
                        "green": 2,
                    }
                },
                "optional": 2,
            }),
        );

        let enums1 = enums();
        let objs = objects();
        let fm = DefaultsValidator::new(&enums1, &objs);
        fm.validate_prop_defaults(&prop)?;

        prop.default = json!({
            "int": 1,
            "string": "bobo",
            "enum": "green",
            "list": [true, false, true],
            "nestedObj": {
                "enumMap": {
                    "blue": 1,
                    "green": "Wrong type!"
                }
            }
        });
        fm.validate_prop_defaults(&prop).expect_err(
            "Should error out because the nested object has an enumMap with the wrong type",
        );

        prop.default = json!({
            "int": 1,
            "string": "bobo",
            "enum": "green",
            "list": [true, false, true],
            "nestedObj": {
                "enumMap": {
                    "blue": 1,
                    "green": 2,
                }
            },
            "optional": 3,
            "extra-property": 2
        });
        fm.validate_prop_defaults(&prop)
            .expect_err("Should error out because the object has an extra property");

        // This test is missing a `list` property. But that's ok, because we'll get it from the object definition.
        prop.default = json!({
            "int": 1,
            "string": "bobo",
            "enum": "green",
            "nestedObj": {
                "enumMap": {
                    "blue": 1,
                    "green": 2,
                }
            },
            "optional": 2,
        });
        fm.validate_prop_defaults(&prop)?;

        prop.default = json!({
            "int": 1,
            "string": "bobo",
            "enum": "green",
            "list": [true, false, true],
            "nestedObj": {
                "enumMap": {
                    "blue": 1,
                    "green": 2,
                }
            },
        });

        // OK, because we are missing `optional` which is optional anyways
        fm.validate_prop_defaults(&prop)?;
        Ok(())
    }

    #[test]
    fn test_validate_prop_defaults_enum_map_optional() -> Result<()> {
        let prop = PropDef::new(
            "key",
            &TypeRef::EnumMap(
                Box::new(TypeRef::Enum("ButtonColor".into())),
                Box::new(TypeRef::Option(Box::new(TypeRef::Int))),
            ),
            &json!({
                "blue": 1,
            }),
        );
        let enums1 = enums();
        let objs = Default::default();
        let fm = DefaultsValidator::new(&enums1, &objs);
        // OK because the value is optional, and thus it's okay if it's missing (green is missing from the default)
        fm.validate_prop_defaults(&prop)?;
        Ok(())
    }
}

#[cfg(test)]
mod string_alias {

    use super::*;
    use serde_json::json;

    // Does this string belong in the type definition?
    #[test]
    fn test_validate_value() -> Result<()> {
        let sa = TypeRef::StringAlias("Name".to_string());

        // type definition is Name
        let def = sa.clone();
        let value = json!("yes");
        assert!(validate_string_alias_value("yes", &sa, &def, &value));
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        // type definition is Name?
        let def = TypeRef::Option(Box::new(sa.clone()));
        let value = json!("yes");
        assert!(validate_string_alias_value("yes", &sa, &def, &value));
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        let value = json!(null);
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        // type definition is Map<Name, Boolean>
        let def = TypeRef::EnumMap(Box::new(sa.clone()), Box::new(TypeRef::Boolean));
        let value = json!({
            "yes": true,
            "YES": false,
        });
        assert!(validate_string_alias_value("yes", &sa, &def, &value));
        assert!(validate_string_alias_value("YES", &sa, &def, &value));
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        // type definition is Map<String, Name>
        let def = TypeRef::EnumMap(Box::new(TypeRef::String), Box::new(sa.clone()));
        let value = json!({
            "ok": "yes",
            "OK": "YES",
        });
        assert!(validate_string_alias_value("yes", &sa, &def, &value));
        assert!(validate_string_alias_value("YES", &sa, &def, &value));
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        // type definition is List<String>
        let def = TypeRef::List(Box::new(sa.clone()));
        let value = json!(["yes", "YES"]);
        assert!(validate_string_alias_value("yes", &sa, &def, &value));
        assert!(validate_string_alias_value("YES", &sa, &def, &value));
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        // type definition is List<Map<String, Name>>
        let def = TypeRef::List(Box::new(TypeRef::StringMap(Box::new(sa.clone()))));
        let value = json!([{"y": "yes"}, {"Y": "YES"}]);
        assert!(validate_string_alias_value("yes", &sa, &def, &value));
        assert!(validate_string_alias_value("YES", &sa, &def, &value));
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        // type definition is Map<String, List<Name>>
        let def = TypeRef::StringMap(Box::new(TypeRef::List(Box::new(sa.clone()))));
        let value = json!({"y": ["yes"], "Y": ["YES"]});
        assert!(validate_string_alias_value("yes", &sa, &def, &value));
        assert!(validate_string_alias_value("YES", &sa, &def, &value));
        assert!(!validate_string_alias_value("no", &sa, &def, &value));

        Ok(())
    }

    fn objects(nm: &str, props: &[PropDef]) -> BTreeMap<String, ObjectDef> {
        let obj1 = ObjectDef::new(nm, props);
        ObjectDef::into_map(&[obj1])
    }

    fn feature(props: &[PropDef]) -> FeatureDef {
        FeatureDef {
            name: "TestFeature".to_string(),
            props: props.into(),
            ..Default::default()
        }
    }

    #[test]
    fn test_string_alias() -> Result<()> {
        let mate = TypeRef::StringAlias("TeamMate".to_string());
        let the_team = {
            let team = TypeRef::List(Box::new(mate.clone()));
            let value = json!(["Alice", "Bonnie", "Charlie", "Deborah", "Eve"]);

            PropDef::with_string_alias("team", &team, &value, &mate)
        };
        test_with_simple_string_alias(&mate, &the_team)?;
        test_with_objects(&mate, &the_team)?;

        let the_team = {
            let team = TypeRef::EnumMap(Box::new(mate.clone()), Box::new(TypeRef::Boolean));
            let value = json!({"Alice": true, "Bonnie": true, "Charlie": true, "Deborah": true, "Eve": true});

            PropDef::with_string_alias("team", &team, &value, &mate)
        };
        test_with_simple_string_alias(&mate, &the_team)?;
        test_with_objects(&mate, &the_team)?;

        Ok(())
    }

    fn test_with_simple_string_alias(mate: &TypeRef, the_team: &PropDef) -> Result<()> {
        let objs = Default::default();
        let enums = Default::default();
        let validator = DefaultsValidator::new(&enums, &objs);

        // For all these tests, the_team defines the set of strings which are valid TeamMate strings.

        // captain is a TeamMate
        let nm = "captain";
        let t = mate.clone();
        let f = {
            let v = json!("Eve");
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };

        validator.validate_feature_def(&f)?;

        let t = mate.clone();
        let f = {
            let v = json!("Nope");
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        assert!(validator.validate_feature_def(&f).is_err());

        // goalkeeper is an Option<TeamMate>
        let nm = "goalkeeper";
        let t = TypeRef::Option(Box::new(mate.clone()));
        let f = {
            let v = json!(null);
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!("Charlie");
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!("Nope");
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        assert!(validator.validate_feature_def(&f).is_err());

        // defenders are List<TeamMate>
        let nm = "defenders";
        let t = TypeRef::List(Box::new(mate.clone()));

        let f = {
            let v = json!([]);
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!(["Alice", "Charlie"]);
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!(["Alice", "Nope"]);
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        assert!(validator.validate_feature_def(&f).is_err());

        // injury-status are Map<TeamMate, Boolean>
        let nm = "injury-status";
        let t = TypeRef::EnumMap(Box::new(mate.clone()), Box::new(TypeRef::Boolean));
        let f = {
            let v = json!({"Bonnie": false, "Deborah": true});
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!({"Bonnie": false, "Nope": true});
            feature(&[the_team.clone(), PropDef::new(nm, &t, &v)])
        };
        assert!(validator.validate_feature_def(&f).is_err());

        // positions are Map<PositionName, List<TeamMate>>
        let nm = "positions";
        let position = TypeRef::StringAlias("PositionName".to_string());
        let t = TypeRef::EnumMap(
            Box::new(position.clone()),
            Box::new(TypeRef::List(Box::new(mate.clone()))),
        );
        let f = {
            let v = json!({"DEFENDER": ["Bonnie", "Charlie"], "MIDFIELD": ["Alice", "Deborah"], "FORWARD": ["Eve"]});
            feature(&[
                the_team.clone(),
                PropDef::with_string_alias(nm, &t, &v, &position),
            ])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!({"DEFENDER": ["Bonnie", "Charlie"], "MIDFIELD": ["Alice", "Deborah"], "STRIKER": ["Eve"]});
            feature(&[
                the_team.clone(),
                PropDef::with_string_alias(nm, &t, &v, &position),
            ])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!({"DEFENDER": ["Bonnie", "Charlie"], "MIDFIELD": ["Nope", "Deborah"], "STRIKER": ["Eve"]});
            feature(&[
                the_team.clone(),
                PropDef::with_string_alias(nm, &t, &v, &position),
            ])
        };
        assert!(validator.validate_feature_def(&f).is_err());
        Ok(())
    }

    fn test_with_objects(mate: &TypeRef, the_team: &PropDef) -> Result<()> {
        let position = TypeRef::StringAlias("PositionName".to_string());
        let positions = {
            let nm = "positions";
            let t = TypeRef::EnumMap(
                Box::new(position.clone()),
                Box::new(TypeRef::List(Box::new(mate.clone()))),
            );
            let v = json!({"DEFENDER": ["Bonnie", "Charlie"], "MIDFIELD": ["Alice", "Deborah"], "FORWARD": ["Eve"]});
            PropDef::with_string_alias(nm, &t, &v, &position)
        };

        let objects = objects(
            "Player",
            &[
                PropDef::new("name", mate, &json!("Untested")),
                PropDef::new("position", &position, &json!("Untested")),
            ],
        );
        let enums = Default::default();
        let validator = DefaultsValidator::new(&enums, &objects);

        // newest-player: Player
        let nm = "newest-player";
        let t = TypeRef::Object("Player".to_string());
        let f = {
            let v = json!({"name": "Eve", "position": "FORWARD"});
            feature(&[
                the_team.clone(),
                positions.clone(),
                PropDef::new(nm, &t, &v),
            ])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!({"name": "Nope", "position": "FORWARD"});
            feature(&[
                the_team.clone(),
                positions.clone(),
                PropDef::new(nm, &t, &v),
            ])
        };
        assert!(validator.validate_feature_def(&f).is_err());

        // positions: List<PositionName>
        // players: Map<TeamMateName, Player>
        let positions = {
            let t = TypeRef::List(Box::new(position.clone()));
            let v = json!(["FORWARD", "DEFENDER"]);
            PropDef::with_string_alias("positions", &t, &v, &position)
        };
        let nm = "players";
        let t = TypeRef::EnumMap(
            Box::new(mate.clone()),
            Box::new(TypeRef::Object("Player".to_string())),
        );
        let f = {
            let v = json!({ "Eve": {"name": "Eve", "position": "FORWARD"}});
            feature(&[
                positions.clone(),
                PropDef::with_string_alias(nm, &t, &v, mate),
            ])
        };
        validator.validate_feature_def(&f)?;

        let f = {
            let v = json!({ "Nope": {"name": "Eve", "position": "FORWARD"}});
            feature(&[
                positions.clone(),
                PropDef::with_string_alias(nm, &t, &v, mate),
            ])
        };
        assert!(validator.validate_feature_def(&f).is_err());

        Ok(())
    }
}

#[cfg(test)]
mod test_gecko_prefs {
    use serde_json::json;

    use crate::error::FMLError;
    use crate::intermediate_representation::{GeckoPrefDef, PrefBranch};

    use super::*;

    #[test]
    fn test_validate_default_mutually_exclusive_with_gecko_pref() {
        let mut prop = PropDef::new("var", &TypeRef::String, &json!("default-value"));
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
        let validator = DefaultsValidator::new(&enums, &objs);

        assert!(matches!(
            validator.validate_feature_def(&feature),
            Err(FMLError::ValidationError(path, reason)) if path == "features/Feature.var" && reason == "gecko-pref and default are mutually exclusive",
        ));

        let validator = DefaultsValidator::new(&enums, &objs).with_lax_gecko_pref_validation(true);
        assert!(matches!(validator.validate_feature_def(&feature), Ok(())));
    }
}
