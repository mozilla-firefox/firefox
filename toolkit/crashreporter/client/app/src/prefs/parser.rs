/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Prefs file parsing utilities.
//!
//! This isn't as extensive as the Rust libpref parser, but we don't need it to be. We _could_
//! potentially use that library, but we're only interested in a subset of prefs so we don't need to
//! parse everything. That doesn't support "lockPref" yet, which I've seen in debian package patches
//! and Enterprise code, though Enterprise hasn't changed libpref.
//!
//! This parsing also permits any pref function to be used in user pref files, whereas the libpref
//! parsing only allows `user_pref`.

use regex::Regex;

const PREF_FUNCTION_NAMES: &[&str] = &["user_pref", "pref", "sticky_pref", "lockPref"];

/// A pattern for a specific preference's value in a prefs file.
///
/// This isn't a fully compliant parser, but it should work for reasonable prefs files.
///
/// Example:
/// ```rust
/// let pattern = PrefPattern::new("FOOBAR").unwrap();
/// assert_eq!(pattern.find(r#"user_pref("FOOBAR", "baz");"#), Some(PrefValue::String(r#""baz""#)));
/// ```
pub struct PrefPattern {
    regex: Regex,
}

/// The value of a pref.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum PrefValue<'a> {
    /// A boolean value (literal "true" or "false").
    Bool(bool),
    /// An integer value.
    Int(i64),
    /// A string value.
    ///
    /// Warning: escape sequences are _not_ checked (we don't expect to see any in the values we
    /// currently read).
    String(&'a str),
}

impl<'a> PrefValue<'a> {
    /// Get the value if it is a Bool.
    pub fn into_bool(self) -> Option<bool> {
        if let Self::Bool(b) = self {
            Some(b)
        } else {
            None
        }
    }

    /// Get the value if it is an Int.
    #[allow(unused)]
    pub fn into_int(self) -> Option<i64> {
        if let Self::Int(i) = self {
            Some(i)
        } else {
            None
        }
    }

    /// Get the value if it is a String.
    pub fn into_str(self) -> Option<&'a str> {
        if let Self::String(s) = self {
            Some(s)
        } else {
            None
        }
    }
}

impl PrefPattern {
    /// Create a new pattern matching the given pref key.
    pub fn new(key: &str) -> Result<Self, regex::Error> {
        // Use debug-formatting to create a quoted string and regex-escape it.
        let escaped = regex::escape(&format!("{key:?}"));
        // We don't escape the function names because we know their values don't need it (it
        // avoids extra allocations for the strings and a Vec to join over).
        let func = PREF_FUNCTION_NAMES.join("|");
        let regex = Regex::new(&format!(
            r"(?x)
            # function name with optional whitespace and open paren
            \b(?:{func})\s*\(
            # Pref name string (optional whitespace) and comma
            \s*{escaped}\s*,
            # Pref value (optional whitespace). This breaks if a string contains a ')'.
            \s*([^)]+)\s*
            # Match a closing paren and semicolon (requiring some valid syntax).
            \)\s*;
        "
        ))?;
        Ok(Self { regex })
    }

    /// Find the value for this pattern in the given prefs content.
    pub fn find<'a>(&self, prefs_content: &'a str) -> Option<PrefValue<'a>> {
        let value_string = self
            .regex
            .captures(prefs_content)
            .and_then(|captures| captures.get(1))
            .map(|m| m.as_str().trim())?;
        if value_string == "false" {
            Some(PrefValue::Bool(false))
        } else if value_string == "true" {
            Some(PrefValue::Bool(true))
        } else if "+-0123456789".contains(value_string.chars().next().unwrap()) {
            value_string.parse().ok().map(PrefValue::Int)
        } else {
            value_string
                .strip_prefix('"')?
                .strip_suffix('"')
                .map(PrefValue::String)
        }
    }
}

// Doctests don't run for binaries, so make some unit tests instead.
#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn missing() {
        let pat = PrefPattern::new("hello").unwrap();
        let input = r#"
            user_pref("rawr", "FOOBAR");
            user_pref("FOOBAR", "hello");
        "#;
        assert_eq!(pat.find(input), None);
    }

    #[test]
    fn malformed() {
        let pat = PrefPattern::new("FOOBAR").unwrap();
        let input = r#"
            user_pref("FOOBAR", "hi")
        "#;
        assert_eq!(pat.find(input), None);
    }

    #[test]
    fn string_pref() {
        let pat = PrefPattern::new("FOOBAR").unwrap();
        let input = r#"user_pref("FOOBAR", "baz");"#;
        assert_eq!(pat.find(input), Some(PrefValue::String("baz")));
    }

    #[test]
    fn precise_search() {
        let pat = PrefPattern::new("FOOBAR").unwrap();
        let input = r#"
            user_pref("rawr", "FOOBAR");
            user_pref("FOOBAR", "baz");
        "#;
        assert_eq!(pat.find(input), Some(PrefValue::String("baz")));
    }

    #[test]
    fn bool_pref() {
        let pat = PrefPattern::new("FOOBAR").unwrap();
        assert_eq!(
            pat.find(r#"user_pref("FOOBAR", true);"#),
            Some(PrefValue::Bool(true))
        );
        assert_eq!(
            pat.find(r#"user_pref("FOOBAR", false);"#),
            Some(PrefValue::Bool(false))
        );
    }

    #[test]
    fn int_pref() {
        let pat = PrefPattern::new("FOOBAR").unwrap();
        assert_eq!(
            pat.find(r#"user_pref("FOOBAR", 42);"#),
            Some(PrefValue::Int(42))
        );
        assert_eq!(
            pat.find(r#"user_pref("FOOBAR", -10);"#),
            Some(PrefValue::Int(-10))
        );
        assert_eq!(
            pat.find(r#"user_pref("FOOBAR", +123);"#),
            Some(PrefValue::Int(123))
        );
    }

    #[test]
    fn pref_functions() {
        let pat = PrefPattern::new("A").unwrap();
        assert_eq!(
            pat.find(r#"user_pref("A", true);"#),
            Some(PrefValue::Bool(true))
        );
        assert_eq!(pat.find(r#"pref("A", true);"#), Some(PrefValue::Bool(true)));
        assert_eq!(
            pat.find(r#"sticky_pref("A", true);"#),
            Some(PrefValue::Bool(true))
        );
        assert_eq!(
            pat.find(r#"lockPref("A", true);"#),
            Some(PrefValue::Bool(true))
        );
        assert_eq!(pat.find(r#"somepref("A", true);"#), None);
    }

    #[test]
    fn whitespace() {
        let pat = PrefPattern::new("A").unwrap();
        assert_eq!(
            pat.find(r#" user_pref ( "A" , true ) ; "#),
            Some(PrefValue::Bool(true))
        );
    }
}
