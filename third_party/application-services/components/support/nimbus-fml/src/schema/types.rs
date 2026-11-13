/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::{BTreeMap, HashSet};

use crate::intermediate_representation::{FeatureDef, ObjectDef, TypeFinder, TypeRef};

pub(crate) struct TypeQuery<'a> {
    object_defs: &'a BTreeMap<String, ObjectDef>,
}

impl<'a> TypeQuery<'a> {
    pub(crate) fn new(objs: &'a BTreeMap<String, ObjectDef>) -> Self {
        Self { object_defs: objs }
    }

    pub(crate) fn all_types(&self, feature_def: &FeatureDef) -> HashSet<TypeRef> {
        let mut types = Default::default();
        self.gather_types(&feature_def.all_types(), &mut types);
        types
    }

    fn gather_types(&self, unseen: &HashSet<TypeRef>, seen: &mut HashSet<TypeRef>) {
        for t in unseen {
            if !seen.contains(t) {
                seen.insert(t.clone());
                if let TypeRef::Object(nm) = t {
                    let def = self.object_defs.get(nm).unwrap();
                    self.gather_types(&def.all_types(), seen);
                }
            }
        }
    }
}
