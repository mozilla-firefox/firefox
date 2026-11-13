/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use serde_json::Value;

/// The `ErrorPath` struct is constructed in the default validator to be used
/// to derive where an error has been detected.
///
/// serde_yaml does not keep track of lines and columns so we need to be able to
/// indicate where an error takes place.
///
/// For reporting errors in the manifest on the command line, an error might have a path such as:
///
///  1. `features/messaging.messages['my-message'].MessageData#is-control` expects a boolean,
///  2. `features/homescreen.sections-enabled[HomeScreenSection#pocket]` expects a boolean
///  3. `objects/AwesomeBar.min-search-term`.
///
/// The path to an error is given by `&self.path`.
///
/// The defaults validation is exactly the same as the validation performed on the Feature Configuration
/// JSON in experimenter. Thus, `literals` is a `Vec<String>` of tokens found in JSON, which should in
/// almost all circumstances lead to the correct token being identified by line and column.
///
/// So the corresponding `literals` of a type mismatch error where an integer `1` is used instead
/// of a boolean would be:
///
///  1. `"messages"`, `{`, `"my-message"`, `"is-control"`, `1`
///  2. `"sections-enabled"`, `{`, `"pocket"`, `1`
///
/// `find_err(src: &str)` is used to find the line and column for the final `1` token.
/// Currently `find_err` exists in `inspector.rs`, but this should move (along with reduced visibility
/// of `literals`) in a future commit.
#[derive(Clone)]
pub(crate) struct ErrorPath {
    start_index: Option<usize>,
    literals: Vec<String>,
    pub(crate) path: String,
}

/// Chained Constructors
impl ErrorPath {
    fn new(path: String, literals: Vec<String>) -> Self {
        Self {
            path,
            literals,
            start_index: None,
        }
    }

    pub(crate) fn feature(name: &str) -> Self {
        Self::new(format!("features/{name}"), Default::default())
    }

    pub(crate) fn object(name: &str) -> Self {
        Self::new(format!("objects/{name}"), Default::default())
    }

    pub(crate) fn example(&self, name: &str) -> Self {
        Self::new(
            format!("{}#examples[\"{name}\"]", &self.path),
            self.literals.clone(),
        )
    }

    pub(crate) fn property(&self, prop_key: &str) -> Self {
        Self::new(
            format!("{}.{prop_key}", &self.path),
            append_quoted(&self.literals, prop_key),
        )
    }

    pub(crate) fn enum_map_key(&self, enum_: &str, key: &str) -> Self {
        Self::new(
            format!("{}[{enum_}#{key}]", &self.path),
            append(&self.literals, &["{".to_string(), format!("\"{key}\"")]),
        )
    }

    pub(crate) fn map_key(&self, key: &str) -> Self {
        Self::new(
            format!("{}['{key}']", &self.path),
            append(&self.literals, &["{".to_string(), format!("\"{key}\"")]),
        )
    }

    pub(crate) fn array_index(&self, index: usize) -> Self {
        let mut literals = append1(&self.literals, "[");
        if index > 0 {
            literals.extend_from_slice(&[",".repeat(index)]);
        }
        Self::new(format!("{}[{index}]", &self.path), literals)
    }

    pub(crate) fn object_value(&self, name: &str) -> Self {
        Self::new(
            format!("{}#{name}", &self.path),
            append1(&self.literals, "{"),
        )
    }

    pub(crate) fn open_brace(&self) -> Self {
        Self::new(self.path.clone(), append1(&self.literals, "{"))
    }

    pub(crate) fn final_error_quoted(&self, highlight: &str) -> Self {
        Self::new(self.path.clone(), append_quoted(&self.literals, highlight))
    }

    pub(crate) fn final_error_value(&self, value: &Value) -> Self {
        let len = self.literals.len();
        let mut literals = Vec::with_capacity(len * 2);
        literals.extend_from_slice(self.literals.as_slice());
        collect_path(&mut literals, value);

        Self {
            path: self.path.clone(),
            literals,
            start_index: Some(len),
        }
    }
}

fn collect_path(literals: &mut Vec<String>, value: &Value) {
    match value {
        Value::Bool(_) | Value::Number(_) | Value::Null => literals.push(value.to_string()),
        Value::String(s) => literals.push(format!("\"{s}\"")),

        Value::Array(array) => {
            literals.push(String::from("["));
            for v in array {
                collect_path(literals, v);
            }
            literals.push(String::from("]"));
        }

        Value::Object(map) => {
            literals.push(String::from("{"));
            if let Some((k, v)) = map.iter().next_back() {
                literals.push(format!("\"{k}\""));
                collect_path(literals, v);
            }
            literals.push(String::from("}"));
        }
    }
}

/// Accessors
impl ErrorPath {
    pub(crate) fn error_token_abbr(&self) -> String {
        match self.start_index {
            Some(index) if index < self.literals.len() - 1 => {
                let start = self
                    .literals
                    .get(index)
                    .map(String::as_str)
                    .unwrap_or_default();
                let end = self.last_error_token().unwrap();
                format!("{start}…{end}")
            }
            _ => self.last_error_token().unwrap().to_owned(),
        }
    }

    pub(crate) fn last_error_token(&self) -> Option<&str> {
        self.literals.last().map(String::as_str)
    }
}

#[cfg(feature = "client-lib")]
impl ErrorPath {
    pub(crate) fn first_error_token(&self) -> Option<&str> {
        if let Some(index) = self.start_index {
            self.literals.get(index).map(String::as_str)
        } else {
            self.last_error_token()
        }
    }

    /// Gives the span of characters within the given source code where this error
    /// was detected.
    ///
    /// Currently, this is limited to finding the last token and adding the length.
    pub(crate) fn error_span(&self, src: &str) -> crate::editing::CursorSpan {
        use crate::editing::CursorPosition;
        let mut lines = src.lines().peekable();
        let last_token = self.last_error_token().unwrap();
        if let Some(index) = self.start_index {
            let path_to_first = self.literals[..index + 1].iter().map(String::as_str);
            let rest = self.literals[index + 1..].iter().map(String::as_str);

            let pos = line_col_from_lines(&mut lines, (0, 0), path_to_first);
            let from: CursorPosition = pos.into();

            let to: CursorPosition = line_col_from_lines(&mut lines, pos, rest).into();

            from + (to + last_token)
        } else {
            let from: CursorPosition =
                line_col_from_lines(&mut lines, (0, 0), self.literals.iter().map(String::as_str))
                    .into();
            from + last_token
        }
    }
}

fn append(original: &[String], new: &[String]) -> Vec<String> {
    let mut clone = Vec::with_capacity(original.len() + new.len());
    clone.extend_from_slice(original);
    clone.extend_from_slice(new);
    clone
}

fn append1(original: &[String], new: &str) -> Vec<String> {
    let mut clone = Vec::with_capacity(original.len() + 1);
    clone.extend_from_slice(original);
    clone.push(new.to_string());
    clone
}

fn append_quoted(original: &[String], new: &str) -> Vec<String> {
    append1(original, &format!("\"{new}\""))
}

#[cfg(feature = "client-lib")]
fn line_col_from_lines<'a>(
    lines: &mut std::iter::Peekable<impl Iterator<Item = &'a str>>,
    start: (usize, usize),
    path: impl Iterator<Item = &'a str>,
) -> (usize, usize) {
    let (mut line_no, mut col_no) = start;

    // `first_match` is "are we looking for the first match of the line"
    let mut first_match = col_no == 0;

    for p in path {
        loop {
            if let Some(line) = lines.peek() {
                // If we haven't had our first match of the line, then start there at the beginning.
                // Otherwise, start one char on from where we were last time.
                //
                // We might optimize this by adding the grapheme length to col_no,
                // but we're in the "make it right" phase.
                let start = if first_match { 0 } else { col_no + 1 };

                if let Some(i) = find_index(line, p, start) {
                    col_no = i;
                    first_match = false;
                    break;
                } else if lines.next().is_some() {
                    // we try the next line!
                    line_no += 1;
                    first_match = true;
                    col_no = 0;
                }
            } else {
                // we've run out of lines, so we should return
                return (0, 0);
            }
        }
    }

    (line_no, col_no)
}

/// Find the index in `line` of the next instance of `pattern`, after `start`
///
#[cfg(feature = "client-lib")]
fn find_index(line: &str, pattern: &str, start: usize) -> Option<usize> {
    use icu_segmenter::GraphemeClusterSegmenter;

    let segmenter = GraphemeClusterSegmenter::new();

    // Convert line boundaries to grapheme slices
    let line_boundaries: Vec<usize> = segmenter.segment_str(line).collect();
    let line: Vec<&str> = line_boundaries
        .windows(2)
        .map(|w| &line[w[0]..w[1]])
        .collect();
    let line_from_start = &line[start..];

    // Convert pattern boundaries to grapheme slices
    let pattern_boundaries: Vec<usize> = segmenter.segment_str(pattern).collect();
    let pattern: Vec<&str> = pattern_boundaries
        .windows(2)
        .map(|w| &pattern[w[0]..w[1]])
        .collect();
    let pattern = pattern.as_slice();

    line_from_start
        .windows(pattern.len())
        .position(|window| window == pattern)
        .map(|i| i + start)
}

#[cfg(feature = "client-lib")]
#[cfg(test)]
mod construction_tests {
    use serde_json::json;

    use super::ErrorPath;

    #[test]
    fn test_property() {
        let path = ErrorPath::feature("my-feature").property("my-property");
        assert_eq!("features/my-feature.my-property", &path.path);
        assert_eq!(&["\"my-property\""], path.literals.as_slice());

        let path = ErrorPath::object("MyObject").property("my-property");
        assert_eq!("objects/MyObject.my-property", &path.path);
        assert_eq!(&["\"my-property\""], path.literals.as_slice());
    }

    #[test]
    fn test_map_key() {
        let path = ErrorPath::feature("my-feature")
            .property("my-map")
            .map_key("my-key");
        assert_eq!("features/my-feature.my-map['my-key']", &path.path);
        assert_eq!(&["\"my-map\"", "{", "\"my-key\""], path.literals.as_slice());
    }

    #[test]
    fn test_enum_map_key() {
        let path = ErrorPath::feature("my-feature")
            .property("my-map")
            .enum_map_key("MyEnum", "my-variant");
        assert_eq!("features/my-feature.my-map[MyEnum#my-variant]", &path.path);
        assert_eq!(
            &["\"my-map\"", "{", "\"my-variant\""],
            path.literals.as_slice()
        );
    }

    #[test]
    fn test_array_index() {
        let path = ErrorPath::feature("my-feature")
            .property("my-array")
            .array_index(1);
        assert_eq!("features/my-feature.my-array[1]", &path.path);
        assert_eq!(&["\"my-array\"", "[", ","], path.literals.as_slice());

        let path = ErrorPath::feature("my-feature")
            .property("my-array")
            .array_index(0);
        assert_eq!("features/my-feature.my-array[0]", &path.path);
        assert_eq!(&["\"my-array\"", "["], path.literals.as_slice());
    }

    #[test]
    fn test_object_value() {
        let path = ErrorPath::feature("my-feature")
            .property("my-object")
            .object_value("MyObject");
        assert_eq!("features/my-feature.my-object#MyObject", &path.path);
        assert_eq!(&["\"my-object\"", "{"], path.literals.as_slice());
    }

    #[test]
    fn test_final_error() {
        //  1. `features/messaging.messages['my-message']#MessageData.is-control` expects a boolean,
        let path = ErrorPath::feature("messaging")
            .property("messages")
            .map_key("my-message")
            .object_value("MessageData")
            .property("is-control")
            .final_error_value(&json!(1));
        assert_eq!(
            "features/messaging.messages['my-message']#MessageData.is-control",
            &path.path
        );
        assert_eq!(
            &[
                "\"messages\"",
                "{",
                "\"my-message\"",
                "{",
                "\"is-control\"",
                "1"
            ],
            path.literals.as_slice()
        );

        //  2. `features/homescreen.sections-enabled[HomeScreenSection#pocket]` expects a boolean
        let path = ErrorPath::feature("homescreen")
            .property("sections-enabled")
            .enum_map_key("HomeScreenSection", "pocket")
            .final_error_value(&json!(1));
        assert_eq!(
            "features/homescreen.sections-enabled[HomeScreenSection#pocket]",
            &path.path
        );

        assert_eq!(
            &["\"sections-enabled\"", "{", "\"pocket\"", "1"],
            path.literals.as_slice()
        );
    }

    #[test]
    fn test_final_error_value_scalars() {
        let path = ErrorPath::feature("my-feature").property("is-enabled");

        let observed = {
            let value = json!(true);
            path.final_error_value(&value)
        };
        assert_eq!(observed.literals.as_slice(), &["\"is-enabled\"", "true"]);

        let observed = {
            let value = json!(13);
            path.final_error_value(&value)
        };
        assert_eq!(observed.literals.as_slice(), &["\"is-enabled\"", "13"]);

        let observed = {
            let value = json!("string");
            path.final_error_value(&value)
        };
        assert_eq!(
            observed.literals.as_slice(),
            &["\"is-enabled\"", "\"string\""]
        );
    }

    #[test]
    fn test_final_error_value_arrays() {
        let path = ErrorPath::feature("my-feature").property("is-enabled");

        let observed = {
            let value = json!([]);
            let o = path.final_error_value(&value);
            assert_eq!(o.first_error_token(), Some("["));
            o
        };
        assert_eq!(observed.literals.as_slice(), &["\"is-enabled\"", "[", "]"]);

        let observed = {
            let value = json!([1, 2]);
            let o = path.final_error_value(&value);
            assert_eq!(o.first_error_token(), Some("["));
            o
        };
        assert_eq!(
            observed.literals.as_slice(),
            &["\"is-enabled\"", "[", "1", "2", "]"]
        );
    }

    #[test]
    fn test_final_error_value_objects() {
        let path = ErrorPath::feature("my-feature").property("is-enabled");

        let observed = {
            let value = json!({});
            let o = path.final_error_value(&value);
            assert_eq!(o.first_error_token(), Some("{"));
            o
        };
        assert_eq!(observed.literals.as_slice(), &["\"is-enabled\"", "{", "}"]);

        let observed = {
            let value = json!({"last": true});
            let o = path.final_error_value(&value);
            assert_eq!(o.first_error_token(), Some("{"));
            o
        };
        assert_eq!(
            observed.literals.as_slice(),
            &["\"is-enabled\"", "{", "\"last\"", "true", "}"]
        );

        let observed = {
            let value = json!({"first": true, "last": true});
            let o = path.final_error_value(&value);
            assert_eq!(o.first_error_token(), Some("{"));
            o
        };
        assert_eq!(
            observed.literals.as_slice(),
            &["\"is-enabled\"", "{", "\"last\"", "true", "}"]
        );
    }
}

#[cfg(feature = "client-lib")]
#[cfg(test)]
mod line_col_tests {

    use super::*;
    use crate::error::Result;

    fn line_col<'a>(src: &'a str, path: impl Iterator<Item = &'a str>) -> (usize, usize) {
        let mut lines = src.lines().peekable();
        line_col_from_lines(&mut lines, (0, 0), path)
    }

    #[test]
    fn test_find_err() -> Result<()> {
        fn do_test(s: &str, path: &[&str], expected: (usize, usize)) {
            let p = path.last().unwrap();
            let path = path.iter().cloned();
            let from = line_col(s, path);
            assert_eq!(from, expected, "Can't find \"{p}\" at {expected:?} in {s}");
        }

        fn do_multi(s: &[&str], path: &[&str], expected: (usize, usize)) {
            let s = s.join("\n");
            do_test(&s, path, expected);
        }

        do_test("ab cd", &["cd"], (0, 3));
        do_test("ab cd", &["ab", "cd"], (0, 3));
        do_test("áط ¢đ εƒ gի", &["áط", "¢đ"], (0, 3));

        do_test("ab ab", &["ab"], (0, 0));
        do_test("ab ab", &["ab", "ab"], (0, 3));

        do_multi(
            &["ab xx cd", "xx ef xx gh", "ij xx"],
            &["ab", "cd", "gh", "xx"],
            (2, 3),
        );

        do_multi(
            &[
                "{",                       // 0
                "  boolean: true,",        // 1
                "  object: {",             // 2
                "    integer: \"string\"", // 3
                "  }",                     // 4
                "}",                       // 5
            ],
            &["object", "integer", "\"string\""],
            (3, 13),
        );

        // pathological case
        do_multi(
            &[
                "{",                       // 0
                "  boolean: true,",        // 1
                "  object: {",             // 2
                "    integer: 1,",         // 3
                "    astring: \"string\"", // 4
                "  },",                    // 5
                "  integer: \"string\"",   // 6
                "}",                       // 7
            ],
            &["integer", "\"string\""],
            (4, 13),
        );

        // With unicode tokens (including R2L)
        do_multi(&["áط ab", "¢đ cd", "εƒ ef", "gh gի"], &["áط", "cd"], (1, 3));

        // Pseudolocalized pangrams, as a small fuzz test
        do_multi(
            &[
                "Wàłţż, Waltz,",
                "bâđ bad",
                "ņÿmƥĥ, nymph,",
                "ƒőŕ for",
                "qüíĉķ quick",
                "ĵíğş jigs",
                "vęx vex",
            ],
            &["bad", "nymph"],
            (2, 7),
        );

        Ok(())
    }

    #[test]
    fn test_find_index_from() -> Result<()> {
        assert_eq!(find_index("012345601", "01", 0), Some(0));
        assert_eq!(find_index("012345601", "01", 1), Some(7));
        assert_eq!(find_index("012345602", "01", 1), None);
        assert_eq!(find_index("åéîø token", "token", 0), Some(5));
        Ok(())
    }
}

#[cfg(feature = "client-lib")]
#[cfg(test)]
mod integration_tests {

    use serde_json::json;

    use super::*;

    fn test_error_span(src: &[&str], path: &ErrorPath, from: (usize, usize), to: (usize, usize)) {
        test_error_span_string(src.join("\n"), path, from, to);
    }

    fn test_error_span_oneline(
        src: &[&str],
        path: &ErrorPath,
        from: (usize, usize),
        to: (usize, usize),
    ) {
        test_error_span_string(src.join(""), path, from, to);
    }

    fn test_error_span_string(
        src: String,
        path: &ErrorPath,
        from: (usize, usize),
        to: (usize, usize),
    ) {
        let observed = path.error_span(src.as_str());

        assert_eq!(
            observed.from,
            from.into(),
            "Incorrectly found first error token \"{p}\" starts at {from:?} in {src}",
            from = observed.from,
            p = path.first_error_token().unwrap()
        );
        assert_eq!(
            observed.to,
            to.into(),
            "Incorrectly found last error token \"{p}\" ends at {to:?} in {src}",
            p = path.last_error_token().unwrap(),
            to = observed.to,
        );
    }

    #[test]
    fn test_last_token() {
        let path = ErrorPath::feature("test-feature")
            .property("integer")
            .final_error_quoted("string");
        let src = &[
            // 01234567890123456789012345
            r#"{"#,                     // 0
            r#"  "boolean": true,"#,    // 1
            r#"  "integer": "string""#, // 2
            r#"}"#,                     // 3
        ];

        test_error_span(src, &path, (2, 13), (2, 21));
        test_error_span_oneline(src, &path, (0, 32), (0, 32 + "string".len() + 2))
    }

    #[test]
    fn test_type_mismatch_scalar() {
        let path = ErrorPath::feature("test-feature")
            .property("boolean")
            .final_error_value(&json!(13));

        let src = &[
            // 01234567890123456789012345
            r#"{"#,                // 0
            r#"  "boolean": 13,"#, // 1
            r#"  "integer": 1"#,   // 2
            r#"}"#,                // 3
        ];
        test_error_span(src, &path, (1, 13), (1, 13 + 2));
    }

    #[test]
    fn test_type_mismatch_error_on_one_line() {
        let path = ErrorPath::feature("test-feature")
            .property("integer")
            .final_error_value(&json!({
                "string": "string"
            }));

        let src = &[
            // 01234567890123456789012345
            r#"{"#,                                    // 0
            r#"  "integer": { "string": "string" },"#, // 1
            r#"  "short": 1,"#,                        // 2
            r#"  "boolean": true,"#,                   // 3
            r#"}"#,                                    // 4
        ];
        test_error_span(
            src,
            &path,
            (1, 13),
            (1, 13 + r#"{ "string": "string" }"#.len()),
        );

        test_error_span_oneline(
            src,
            &path,
            (0, 14),
            (0, 14 + r#"{ "string": "string" }"#.len()),
        );
    }

    #[test]
    fn test_type_mismatch_error_on_multiple_lines() {
        let path = ErrorPath::feature("test-feature").final_error_value(&json!({}));
        let src = &[
            // 012345678
            r#"{ "#, // 0
            r#"  "#, // 1
            r#"  "#, // 2
            r#"  "#, // 3
            r#"} "#, // 4
        ];
        test_error_span(src, &path, (0, 0), (4, 1));
    }

    #[test]
    fn test_error_abbr() {
        let path = ErrorPath::feature("test_feature").final_error_value(&json!(true));
        assert_eq!(path.error_token_abbr().as_str(), "true");

        let path = ErrorPath::feature("test_feature").final_error_value(&json!(42));
        assert_eq!(path.error_token_abbr().as_str(), "42");

        let path = ErrorPath::feature("test_feature").final_error_value(&json!("string"));
        assert_eq!(path.error_token_abbr().as_str(), "\"string\"");

        let path = ErrorPath::feature("test_feature").final_error_value(&json!([]));
        assert_eq!(path.error_token_abbr().as_str(), "[…]");

        let path = ErrorPath::feature("test_feature").final_error_value(&json!({}));
        assert_eq!(path.error_token_abbr().as_str(), "{…}");

        let path = ErrorPath::feature("test_feature").final_error_quoted("foo");
        assert_eq!(path.error_token_abbr().as_str(), "\"foo\"");
    }
}
