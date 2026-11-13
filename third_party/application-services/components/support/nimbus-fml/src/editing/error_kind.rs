/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::BTreeSet;

use serde_json::{Map, Value};

use crate::intermediate_representation::{PropDef, TypeRef};

pub(crate) enum ErrorKind {
    InvalidKey {
        key_type: TypeRef,
        in_use: BTreeSet<String>,
    },
    InvalidPropKey {
        valid: BTreeSet<String>,
        in_use: BTreeSet<String>,
    },
    InvalidValue {
        value_type: TypeRef,
    },
    InvalidNestedValue {
        prop_name: String,
        prop_type: TypeRef,
    },
    TypeMismatch {
        value_type: TypeRef,
    },
}

impl ErrorKind {
    pub(crate) fn invalid_key(type_ref: &TypeRef, map: &Map<String, Value>) -> Self {
        let keys = map.keys().cloned().collect();
        Self::InvalidKey {
            key_type: type_ref.clone(),
            in_use: keys,
        }
    }

    pub(crate) fn invalid_value(type_ref: &TypeRef) -> Self {
        Self::InvalidValue {
            value_type: type_ref.clone(),
        }
    }

    pub(crate) fn invalid_nested_value(prop_name: &str, type_ref: &TypeRef) -> Self {
        Self::InvalidNestedValue {
            prop_name: prop_name.to_owned(),
            prop_type: type_ref.clone(),
        }
    }

    pub(crate) fn invalid_prop(props: &[PropDef], map: &Map<String, Value>) -> Self {
        let keys = map.keys().cloned().collect();
        let props = props.iter().map(|p| p.name.clone()).collect();
        Self::InvalidPropKey {
            valid: props,
            in_use: keys,
        }
    }

    pub(crate) fn type_mismatch(type_ref: &TypeRef) -> Self {
        Self::TypeMismatch {
            value_type: type_ref.clone(),
        }
    }
}

impl ErrorKind {
    pub(crate) fn message(&self, token: &str) -> String {
        match self {
            Self::InvalidKey { key_type: t, .. } => match t {
                TypeRef::String => format!("Invalid key {token}"),
                _ => format!("Invalid key {token} for type {t}"),
            },
            Self::InvalidPropKey { .. } => format!("Invalid property {token}"),
            Self::InvalidValue { value_type: t } => format!("Invalid value {token} for type {t}"),
            Self::InvalidNestedValue {
                prop_name,
                prop_type: t,
            } => {
                format!("A valid value for {prop_name} of type {t} is missing")
            }
            Self::TypeMismatch { value_type: t } => format!("Invalid value {token} for type {t}"),
        }
    }
}
