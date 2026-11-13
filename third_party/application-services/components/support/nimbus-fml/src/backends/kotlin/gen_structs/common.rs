// /* This Source Code Form is subject to the terms of the Mozilla Public
//  * License, v. 2.0. If a copy of the MPL was not distributed with this
//  * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use heck::{ToLowerCamelCase, ToShoutySnakeCase, ToUpperCamelCase};
use std::fmt::Display;

/// Get the idiomatic Kotlin rendering of a class name (for enums, records, errors, etc).
pub fn class_name(nm: &dyn Display) -> String {
    nm.to_string().to_upper_camel_case()
}

/// Get the idiomatic Kotlin rendering of a variable name.
pub fn var_name(nm: &dyn Display) -> String {
    nm.to_string().to_lower_camel_case()
}

/// Get the idiomatic Kotlin rendering of an individual enum variant.
pub fn enum_variant_name(nm: &dyn Display) -> String {
    nm.to_string().to_shouty_snake_case()
}

/// Surrounds a string with quotes.
/// In Kotlin, you can """triple quote""" multi-line strings
/// so you don't have to escape " and \n characters.
pub fn quoted(string: &dyn Display) -> String {
    let string = string.to_string();
    if string.contains('"') || string.contains('\n') {
        format!(r#""""{}""""#, string)
    } else {
        format!(r#""{}""#, string)
    }
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
        // Kotlin takes the `?` as an indicator to that the preceeding expression
        // is optional to continue processing, but be aware that the
        // expression returns an optional.
        // Only the value_getter returns an optional, yet the optionality propagates.
        // https://kotlinlang.org/docs/null-safety.html#safe-calls
        let getter = match (mapper, merger) {
            (Some(mapper), Some(merger)) => format!("{}?.{}?.{}", getter, mapper, merger),
            (Some(mapper), None) => format!("{}?.{}", getter, mapper),
            (None, Some(merger)) => format!("{}?.{}", getter, merger),
            (None, None) => getter,
        };

        format!(
            "{getter} ?: {fallback}",
            getter = getter,
            fallback = default
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
        Some(format!("let({})", transform))
    }
}

#[cfg(test)]
mod unit_tests {
    use super::*;

    #[test]
    fn test_quoted() {
        assert_eq!(
            quoted(&"no-quotes".to_string()),
            "\"no-quotes\"".to_string()
        );
        assert_eq!(
            quoted(&"a \"quoted\" string".to_string()),
            "\"\"\"a \"quoted\" string\"\"\"".to_string()
        );
    }
}
