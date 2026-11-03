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

use once_cell::sync::Lazy;
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
        // We don't regex::escape the function names because we know their contents don't need it
        // (it avoids extra allocations for the strings and a Vec to join over).
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
    pub fn find<'a>(&self, prefs_content: &'a PrefsString) -> Option<PrefValue<'a>> {
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

    /// Convenience function for asserting that find("string") matches a value
    #[cfg(test)]
    fn assert_find_eq(&self, content: &str, value: Option<PrefValue<'static>>) {
        let content = content.into();
        assert_eq!(self.find(&content), value);
    }
}

/// A string of prefs content.
///
/// This will remove comments, which is required for subsequent `PrefPattern` searching to operate
/// correctly.
pub struct PrefsString {
    content: String,
}

impl PrefsString {
    pub fn new(mut content: String) -> Self {
        // Replace comments (which may be python- or c-style) with whitespace to avoid additional
        // allocations.
        static COMMENT: Lazy<Regex> = Lazy::new(|| {
            Regex::new(
                r#"(?x)
                (?:
                    # Python or c-style line comments ('#...' or '//...')
                    # Because of verbose mode, this uses \x23 for a '#' character
                    (?:(?:\x23|//) .* (?:$|\n))
                    # Multiline c-style comments ('/*...*/')
                    # The 's' flag allows '.' to match newlines.
                    # We use a non-greedy match to stop at the first comment close token.
                    | (?s:/\* .*? \*/)
                )
            "#,
            )
            .unwrap()
        });
        let ranges = COMMENT
            .find_iter(&content)
            .map(|m| m.range())
            .collect::<Vec<_>>();
        for range in ranges {
            // SAFETY: str::as_bytes_mut() requires that the buffer slice be valid UTF8 after
            // mutation. We are filling the buffer with space characters, which is valid UTF8.
            unsafe { content[range].as_bytes_mut().fill(b' ') };
        }

        Self { content }
    }
}

impl From<String> for PrefsString {
    fn from(value: String) -> Self {
        Self::new(value)
    }
}

impl From<&str> for PrefsString {
    fn from(value: &str) -> Self {
        value.to_owned().into()
    }
}

impl std::ops::Deref for PrefsString {
    type Target = str;
    fn deref(&self) -> &Self::Target {
        &self.content
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn missing() {
        PrefPattern::new("hello").unwrap().assert_find_eq(
            r#"
                user_pref("rawr", "FOOBAR");
                user_pref("FOOBAR", "hello");
            "#,
            None,
        );
    }

    #[test]
    fn malformed() {
        PrefPattern::new("FOOBAR")
            .unwrap()
            .assert_find_eq(r#"user_pref("FOOBAR", "hi")"#, None);
    }

    #[test]
    fn string_pref() {
        PrefPattern::new("FOOBAR").unwrap().assert_find_eq(
            r#"user_pref("FOOBAR", "baz");"#,
            Some(PrefValue::String("baz")),
        );
    }

    #[test]
    fn precise_search() {
        PrefPattern::new("FOOBAR").unwrap().assert_find_eq(
            r#"
                user_pref("rawr", "FOOBAR");
                user_pref("FOOBAR", "baz");
            "#,
            Some(PrefValue::String("baz")),
        );
    }

    #[test]
    fn bool_pref() {
        let pat = PrefPattern::new("FOOBAR").unwrap();
        pat.assert_find_eq(r#"user_pref("FOOBAR", true);"#, Some(PrefValue::Bool(true)));
        pat.assert_find_eq(
            r#"user_pref("FOOBAR", false);"#,
            Some(PrefValue::Bool(false)),
        );
    }

    #[test]
    fn int_pref() {
        let pat = PrefPattern::new("FOOBAR").unwrap();
        pat.assert_find_eq(r#"user_pref("FOOBAR", 42);"#, Some(PrefValue::Int(42)));
        pat.assert_find_eq(r#"user_pref("FOOBAR", -10);"#, Some(PrefValue::Int(-10)));
        pat.assert_find_eq(r#"user_pref("FOOBAR", +123);"#, Some(PrefValue::Int(123)));
    }

    #[test]
    fn pref_functions() {
        let pat = PrefPattern::new("A").unwrap();
        pat.assert_find_eq(r#"user_pref("A", true);"#, Some(PrefValue::Bool(true)));
        pat.assert_find_eq(r#"pref("A", true);"#, Some(PrefValue::Bool(true)));
        pat.assert_find_eq(r#"sticky_pref("A", true);"#, Some(PrefValue::Bool(true)));
        pat.assert_find_eq(r#"lockPref("A", true);"#, Some(PrefValue::Bool(true)));
        pat.assert_find_eq(r#"somepref("A", true);"#, None);
    }

    #[test]
    fn whitespace() {
        PrefPattern::new("A").unwrap().assert_find_eq(
            r#" user_pref ( "A" , true ) ; "#,
            Some(PrefValue::Bool(true)),
        );
    }

    #[test]
    fn same_line() {
        PrefPattern::new("A").unwrap().assert_find_eq(
            r#"user_pref("B", "foo");user_pref("A", true);"#,
            Some(PrefValue::Bool(true)),
        );
    }

    #[test]
    fn comments() {
        let pat = PrefPattern::new("A").unwrap();
        pat.assert_find_eq(r#"#user_pref("A", true);"#, None);
        pat.assert_find_eq(r#"//user_pref("A", true);"#, None);
        pat.assert_find_eq(r#"/*user_pref("A", true);*/"#, None);
        pat.assert_find_eq("/*\nuser_pref(\"A\", true);\n*/", None);
        pat.assert_find_eq(
            r#"/*
            foo bar
            user_pref("A", true);
            */"#,
            None,
        );
        pat.assert_find_eq(
            r#"
            //user_pref("A", true);
            user_pref("A", false);
            "#,
            Some(PrefValue::Bool(false)),
        );
        pat.assert_find_eq(
            r#"
            user_pref("A", true);
            //user_pref("A", false);
            "#,
            Some(PrefValue::Bool(true)),
        );
        pat.assert_find_eq(
            r#"user_pref("A", false);#user_pref("A", true);"#,
            Some(PrefValue::Bool(false)),
        );
        pat.assert_find_eq(
            r#"user_pref("A", false);//user_pref("A", true);"#,
            Some(PrefValue::Bool(false)),
        );
    }
}
