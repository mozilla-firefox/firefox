/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use sha2::{Digest, Sha256};

use crate::intermediate_representation::{
    EnumDef, FeatureDef, ObjectDef, PropDef, TypeRef, VariantDef,
};
use std::{
    collections::{BTreeMap, HashSet},
    hash::{Hash, Hasher},
};

use super::TypeQuery;

pub(crate) struct SchemaHasher<'a> {
    enum_defs: &'a BTreeMap<String, EnumDef>,
    object_defs: &'a BTreeMap<String, ObjectDef>,
}

impl<'a> SchemaHasher<'a> {
    pub(crate) fn new(
        enums: &'a BTreeMap<String, EnumDef>,
        objs: &'a BTreeMap<String, ObjectDef>,
    ) -> Self {
        Self {
            enum_defs: enums,
            object_defs: objs,
        }
    }

    pub(crate) fn hash(&self, feature_def: &FeatureDef) -> u64 {
        let mut hasher: Sha256Hasher = Default::default();
        feature_def.schema_hash(&mut hasher);

        let types = self.all_types(feature_def);

        // We iterate through the object_defs, then the enum_defs because they are both
        // ordered, and we want to maintain a stable ordering.
        // By contrast, `types`, a HashSet, definitely does not have a stable ordering.
        for (obj_nm, obj_def) in self.object_defs {
            if types.contains(&TypeRef::Object(obj_nm.clone())) {
                obj_def.schema_hash(&mut hasher);
            }
        }

        for (enum_nm, enum_def) in self.enum_defs {
            if types.contains(&TypeRef::Enum(enum_nm.clone())) {
                enum_def.schema_hash(&mut hasher);
            }
        }

        hasher.finish()
    }

    fn all_types(&self, feature_def: &FeatureDef) -> HashSet<TypeRef> {
        let all_types = TypeQuery::new(self.object_defs);
        all_types.all_types(feature_def)
    }
}

trait SchemaHash {
    fn schema_hash<H: Hasher>(&self, state: &mut H);
}

impl SchemaHash for FeatureDef {
    fn schema_hash<H: Hasher>(&self, state: &mut H) {
        self.props.schema_hash(state);
        self.allow_coenrollment.hash(state);
    }
}

impl SchemaHash for Vec<PropDef> {
    fn schema_hash<H: Hasher>(&self, state: &mut H) {
        let mut vec: Vec<_> = self.iter().collect();
        vec.sort_by_key(|item| &item.name);

        for item in vec {
            item.schema_hash(state);
        }
    }
}

impl SchemaHash for Vec<VariantDef> {
    fn schema_hash<H: Hasher>(&self, state: &mut H) {
        let mut vec: Vec<_> = self.iter().collect();
        vec.sort_by_key(|item| &item.name);

        for item in vec {
            item.schema_hash(state);
        }
    }
}

impl SchemaHash for PropDef {
    fn schema_hash<H: Hasher>(&self, state: &mut H) {
        self.name.hash(state);
        self.typ.hash(state);
        self.string_alias.hash(state);
    }
}

impl SchemaHash for ObjectDef {
    fn schema_hash<H: Hasher>(&self, state: &mut H) {
        self.props.schema_hash(state);
    }
}

impl SchemaHash for EnumDef {
    fn schema_hash<H: Hasher>(&self, state: &mut H) {
        self.variants.schema_hash(state);
    }
}

impl SchemaHash for VariantDef {
    fn schema_hash<H: Hasher>(&self, state: &mut H) {
        self.name.hash(state);
    }
}

#[derive(Default)]
pub(crate) struct Sha256Hasher {
    hasher: Sha256,
}

impl std::hash::Hasher for Sha256Hasher {
    fn finish(&self) -> u64 {
        let v = self.hasher.clone().finalize();
        u64::from_le_bytes(v[0..8].try_into().unwrap())
    }

    fn write(&mut self, bytes: &[u8]) {
        self.hasher.update(bytes);
    }
}

#[cfg(test)]
mod unit_tests {

    use crate::error::Result;
    use serde_json::json;

    use super::*;

    #[test]
    fn test_simple_schema_is_stable() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();

        let prop1 = PropDef::new("p1", &TypeRef::String, &json!("No"));
        let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(42));

        let feature_def =
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false);
        let mut prev: Option<u64> = None;
        for _ in 0..100 {
            let hasher = SchemaHasher::new(&enums, &objs);
            let hash = hasher.hash(&feature_def);
            if let Some(prev) = prev {
                assert_eq!(prev, hash);
            }
            prev = Some(hash);
        }

        Ok(())
    }

    #[test]
    fn test_simple_schema_is_stable_with_props_in_any_order() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();

        let prop1 = PropDef::new("p1", &TypeRef::String, &json!("No"));
        let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(42));

        let f1 = {
            FeatureDef::new(
                "test_feature",
                "documentation",
                vec![prop1.clone(), prop2.clone()],
                false,
            )
        };

        let f2 = { FeatureDef::new("test_feature", "documentation", vec![prop2, prop1], false) };

        let hasher = SchemaHasher::new(&enums, &objs);
        assert_eq!(hasher.hash(&f1), hasher.hash(&f2));

        Ok(())
    }

    #[test]
    fn test_simple_schema_is_stable_changing_defaults() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();

        let f1 = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("No"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(42));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };

        let f2 = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("Nope"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };

        let hasher = SchemaHasher::new(&enums, &objs);
        assert_eq!(hasher.hash(&f1), hasher.hash(&f2));

        Ok(())
    }

    #[test]
    fn test_simple_schema_is_sensitive_to_change() -> Result<()> {
        let enums = Default::default();
        let objs = Default::default();

        let f1 = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("Nope"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };

        let hasher = SchemaHasher::new(&enums, &objs);

        // Sensitive to change in type of properties
        let ne = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("Nope"));
            let prop2 = PropDef::new("p2", &TypeRef::Boolean, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };
        assert_ne!(hasher.hash(&f1), hasher.hash(&ne));

        // Sensitive to change in name of properties
        let ne = {
            let prop1 = PropDef::new("p1_", &TypeRef::String, &json!("Nope"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], false)
        };
        assert_ne!(hasher.hash(&f1), hasher.hash(&ne));

        // Sensitive to change in changes in coenrollment status
        let ne = {
            let prop1 = PropDef::new("p1", &TypeRef::String, &json!("Nope"));
            let prop2 = PropDef::new("p2", &TypeRef::Int, &json!(1));
            FeatureDef::new("test_feature", "documentation", vec![prop1, prop2], true)
        };
        assert_ne!(hasher.hash(&f1), hasher.hash(&ne));

        Ok(())
    }

    #[test]
    fn test_schema_is_sensitive_to_enum_change() -> Result<()> {
        let objs = Default::default();

        let enum_nm = "MyEnum";
        let enum_t = TypeRef::Enum(enum_nm.to_string());

        let f1 = {
            let prop1 = PropDef::new("p1", &enum_t, &json!("one"));
            FeatureDef::new("test_feature", "documentation", vec![prop1], false)
        };

        let enums = {
            let enum1 = EnumDef::new(enum_nm, &["one", "two"]);
            EnumDef::into_map(&[enum1])
        };

        let hasher = SchemaHasher::new(&enums, &objs);
        let h1 = hasher.hash(&f1);

        let enums = {
            let enum1 = EnumDef::new(enum_nm, &["one", "two", "newly-added"]);
            EnumDef::into_map(&[enum1])
        };
        let hasher = SchemaHasher::new(&enums, &objs);
        let ne = hasher.hash(&f1);

        assert_ne!(h1, ne);

        Ok(())
    }

    #[test]
    fn test_schema_is_sensitive_only_to_the_enums_used() -> Result<()> {
        let objs = Default::default();

        let enum_nm = "MyEnum";
        let enum_t = TypeRef::Enum(enum_nm.to_string());

        let f1 = {
            let prop1 = PropDef::new("p1", &enum_t, &json!("one"));
            FeatureDef::new("test_feature", "documentation", vec![prop1], false)
        };

        let enums = {
            let enum1 = EnumDef::new(enum_nm, &["one", "two"]);
            let enums1 = &[enum1];
            EnumDef::into_map(enums1)
        };

        let hasher = SchemaHasher::new(&enums, &objs);
        // Get an original hash here.
        let h1 = hasher.hash(&f1);

        let enums = {
            let enum1 = EnumDef::new(enum_nm, &["one", "two"]);
            // Add an extra enum here.
            let enum2 = EnumDef::new("AnotherEnum", &["one", "two"]);
            let enums1 = &[enum1, enum2];
            EnumDef::into_map(enums1)
        };
        let hasher = SchemaHasher::new(&enums, &objs);
        let h2 = hasher.hash(&f1);

        assert_eq!(h1, h2);

        Ok(())
    }

    #[test]
    fn test_schema_is_sensitive_to_object_change() -> Result<()> {
        let enums = Default::default();
        let obj_nm = "MyObject";
        let obj_t = TypeRef::Object(obj_nm.to_string());

        let f1 = {
            let prop1 = PropDef::new("p1", &obj_t, &json!({}));
            FeatureDef::new("test_feature", "documentation", vec![prop1], false)
        };

        let objs = {
            let obj_def = ObjectDef::new(
                obj_nm,
                &[PropDef::new("obj-p1", &TypeRef::Boolean, &json!(true))],
            );

            ObjectDef::into_map(&[obj_def])
        };

        let hasher = SchemaHasher::new(&enums, &objs);
        // Get an original hash here.
        let h1 = hasher.hash(&f1);

        let objs = {
            let obj_def = ObjectDef::new(
                obj_nm,
                &[
                    PropDef::new("obj-p1", &TypeRef::Boolean, &json!(true)),
                    PropDef::new("obj-p2", &TypeRef::Boolean, &json!(true)),
                ],
            );

            ObjectDef::into_map(&[obj_def])
        };

        let hasher = SchemaHasher::new(&enums, &objs);
        let ne = hasher.hash(&f1);

        assert_ne!(h1, ne);

        Ok(())
    }

    #[test]
    fn test_schema_is_sensitive_only_to_the_objects_used() -> Result<()> {
        let enums = Default::default();

        let obj_nm = "MyObject";
        let obj_t = TypeRef::Object(obj_nm.to_string());

        let f1 = {
            let prop1 = PropDef::new("p1", &obj_t, &json!({}));
            FeatureDef::new("test_feature", "documentation", vec![prop1], false)
        };

        let objects = {
            let obj1 = ObjectDef::new(
                obj_nm,
                &[PropDef::new("obj-p1", &TypeRef::Boolean, &json!(true))],
            );
            ObjectDef::into_map(&[obj1])
        };

        let hasher = SchemaHasher::new(&enums, &objects);
        // Get an original hash here.
        let h1 = hasher.hash(&f1);

        // Now add more objects, that aren't related to this feature.
        let objects = {
            let obj1 = ObjectDef::new(
                obj_nm,
                &[PropDef::new("obj-p1", &TypeRef::Boolean, &json!(true))],
            );
            let obj2 = ObjectDef::new(
                "AnotherObject",
                &[PropDef::new("obj-p1", &TypeRef::Boolean, &json!(true))],
            );
            ObjectDef::into_map(&[obj1, obj2])
        };

        let hasher = SchemaHasher::new(&enums, &objects);
        let h2 = hasher.hash(&f1);

        assert_eq!(h1, h2);

        Ok(())
    }

    #[test]
    fn test_schema_is_sensitive_to_nested_change() -> Result<()> {
        let obj_nm = "MyObject";
        let obj_t = TypeRef::Object(obj_nm.to_string());

        let enum_nm = "MyEnum";
        let enum_t = TypeRef::Enum(enum_nm.to_string());

        let f1 = {
            let prop1 = PropDef::new("p1", &obj_t, &json!({}));
            FeatureDef::new("test_feature", "documentation", vec![prop1], false)
        };

        let objs = {
            let obj_def = ObjectDef::new(obj_nm, &[PropDef::new("obj-p1", &enum_t, &json!("one"))]);

            ObjectDef::into_map(&[obj_def])
        };

        let enums = {
            let enum1 = EnumDef::new(enum_nm, &["one", "two"]);
            EnumDef::into_map(&[enum1])
        };

        let hasher = SchemaHasher::new(&enums, &objs);
        // Get an original hash here.
        let h1 = hasher.hash(&f1);

        // Now change a deeply nested enum variant.
        let enums = {
            let enum1 = EnumDef::new(enum_nm, &["one", "two", "newly-added"]);
            EnumDef::into_map(&[enum1])
        };
        let hasher = SchemaHasher::new(&enums, &objs);
        let ne = hasher.hash(&f1);

        assert_ne!(h1, ne);
        Ok(())
    }
}
