// /* This Source Code Form is subject to the terms of the Mozilla Public
//  * License, v. 2.0. If a copy of the MPL was not distributed with this
//  * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use heck::{ToLowerCamelCase, ToUpperCamelCase};
use std::fmt::Display;

/// Get the idiomatic Swift rendering of a class name (for enums, records, errors, etc).
pub fn class_name(nm: &dyn Display) -> String {
    nm.to_string().to_upper_camel_case()
}

/// Get the idiomatic Swift rendering of a variable name.
pub fn var_name(nm: &dyn Display) -> String {
    nm.to_string().to_lower_camel_case()
}

/// Get the idiomatic Swift rendering of an individual enum variant.
pub fn enum_variant_name(nm: &dyn Display) -> String {
    nm.to_string().to_lower_camel_case()
}

/// Surrounds a property name with quotes. It is assumed that property names do not need escaping.
pub fn quoted(v: &dyn Display) -> String {
    format!(r#""{}""#, v)
}

pub(crate) mod code_type {
    use std::fmt::Display;

    use crate::backends::{CodeOracle, CodeType};

    /// The language specific expression that gets a value of the `prop` from the `vars` object.
    pub(crate) fn property_getter(
        ct: &dyn CodeType,
        oracle: &dyn CodeOracle,
        vars: &dyn Display,
        prop: &dyn Display,
        default: &dyn Display,
    ) -> String {
        let getter = ct.value_getter(oracle, vars, prop);
        let mapper = ct.value_mapper(oracle);
        let default = ct
            .defaults_mapper(oracle, &default, vars)
            .unwrap_or_else(|| default.to_string());
        let merger = ct.value_merger(oracle, &default);

        // We need to be quite careful about option chaining.
        // Swift takes the `?` as an indicator to _stop evaluating the chain expression_ if the immediately preceeding
        // expression returns an optional.
        // Only the value_getter returns an optional, so that's all we need to `?`.
        // https://docs.swift.org/swift-book/LanguageGuide/OptionalChaining.html
        let getter = match (mapper, merger) {
            (Some(mapper), Some(merger)) => format!("{}?.{}.{}", getter, mapper, merger),
            (Some(mapper), None) => format!("{}?.{}", getter, mapper),
            (None, Some(merger)) => format!("{}?.{}", getter, merger),
            (None, None) => getter,
        };

        format!(
            "{getter} ?? {fallback}",
            getter = getter,
            fallback = default,
        )
    }

    pub(crate) fn value_getter(
        ct: &dyn CodeType,
        oracle: &dyn CodeOracle,
        vars: &dyn Display,
        prop: &dyn Display,
    ) -> String {
        let vt = ct.variables_type(oracle);
        format!(
            "{vars}.get{vt}(\"{prop}\")",
            vars = vars,
            vt = vt,
            prop = prop
        )
    }

    pub(crate) fn value_mapper(ct: &dyn CodeType, oracle: &dyn CodeOracle) -> Option<String> {
        let transform = ct.create_transform(oracle)?;
        Some(format!("map({})", transform))
    }
}
