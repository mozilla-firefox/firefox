/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub use crate::editing::FmlEditorError;
use crate::{
    editing::{CursorPosition, ErrorConverter},
    error::{ClientError, FMLError, Result},
    intermediate_representation::{FeatureDef, FeatureExample, FeatureManifest},
    FmlClient, JsonObject,
};
use serde_json::Value;
use std::sync::Arc;
use url::Url;

impl FmlClient {
    pub fn get_feature_inspector(&self, id: String) -> Option<Arc<FmlFeatureInspector>> {
        _ = self.manifest.find_feature(&id)?;
        Some(Arc::new(FmlFeatureInspector::new(
            self.manifest.clone(),
            id,
        )))
    }
}

pub struct FmlFeatureInspector {
    manifest: Arc<FeatureManifest>,
    feature_id: String,
}

impl FmlFeatureInspector {
    pub(crate) fn new(manifest: Arc<FeatureManifest>, feature_id: String) -> Self {
        Self {
            manifest,
            feature_id,
        }
    }

    pub fn get_default_json(&self) -> Result<JsonObject> {
        let f = self.get_feature();

        match f.default_json() {
            Value::Object(map) => Ok(map),
            _ => Err(FMLError::ClientError(ClientError::InvalidFeatureValue(
                "A non-JSON object is returned as default. This is likely a Nimbus FML bug."
                    .to_string(),
            ))),
        }
    }

    pub fn get_examples(&self) -> Result<Vec<FmlFeatureExample>> {
        let feature_examples = &self.get_feature().examples;
        let mut examples: Vec<FmlFeatureExample> = Vec::with_capacity(feature_examples.len() + 1);
        // Make an FmlFeatureExample out of the FeatureExample, for exposure to foreign languages.
        examples.extend(feature_examples.clone().into_iter().map(Into::into).rev());

        // Add the full defaults for every feature.
        // This will help kick-start adoption.
        examples.push(FmlFeatureExample {
            name: String::from("Default configuration (in full)"),
            value: self.get_default_json()?,
            ..Default::default()
        });

        Ok(examples)
    }

    pub fn get_errors(&self, string: String) -> Option<Vec<FmlEditorError>> {
        match self.parse_json_string(&string) {
            Err(e) => Some(vec![e]),
            Ok(json) => {
                let errors = self.get_semantic_errors(&string, json);
                if errors.is_empty() {
                    None
                } else {
                    Some(errors)
                }
            }
        }
    }

    pub fn get_schema_hash(&self) -> String {
        let (fm, f) = self.get_manifest_and_feature();
        fm.feature_schema_hash(f)
    }

    pub fn get_defaults_hash(&self) -> String {
        let (fm, f) = self.get_manifest_and_feature();
        fm.feature_defaults_hash(f)
    }
}

impl FmlFeatureInspector {
    fn get_feature(&self) -> &FeatureDef {
        self.get_manifest_and_feature().1
    }

    fn _get_manifest(&self) -> &FeatureManifest {
        self.get_manifest_and_feature().0
    }

    fn get_manifest_and_feature(&self) -> (&FeatureManifest, &FeatureDef) {
        self.manifest
            .find_feature(&self.feature_id)
            .expect("We construct an inspector with a feature_id, so this should be impossible")
    }

    fn parse_json_string(&self, string: &str) -> Result<Value, FmlEditorError> {
        Ok(match serde_json::from_str::<Value>(string) {
            Ok(json) if json.is_object() => json,
            Ok(_) => syntax_error("Need valid JSON object", 0, 0, string)?,
            Err(e) => {
                let col = e.column();
                syntax_error(
                    "Need valid JSON object",
                    e.line() - 1,
                    if col == 0 { 0 } else { col - 1 },
                    "",
                )?
            }
        })
    }

    fn get_semantic_errors(&self, src: &str, value: Value) -> Vec<FmlEditorError> {
        let (manifest, feature_def) = self.get_manifest_and_feature();
        let (merged_value, errors) = manifest.merge_and_errors(feature_def, &value);
        if !errors.is_empty() {
            let converter = ErrorConverter::new(&manifest.enum_defs, &manifest.obj_defs);
            converter.convert_into_editor_errors(feature_def, &merged_value, src, &errors)
        } else {
            Default::default()
        }
    }
}

fn syntax_error(
    message: &str,
    line: usize,
    col: usize,
    highlight: &str,
) -> Result<Value, FmlEditorError> {
    let error_span = CursorPosition::new(line, col) + highlight;
    Err(FmlEditorError {
        message: String::from(message),
        error_span,
        line: line as u32,
        col: col as u32,
        ..Default::default()
    })
}

#[derive(Default)]
pub struct FmlFeatureExample {
    pub name: String,
    pub description: Option<String>,
    pub url: Option<Url>,
    pub value: JsonObject,
}

impl From<FeatureExample> for FmlFeatureExample {
    fn from(example: FeatureExample) -> Self {
        let metadata = example.metadata;
        Self {
            name: metadata.name,
            description: metadata.description,
            url: metadata.url,
            value: match example.value {
                Value::Object(v) => v,
                _ => Default::default(),
            },
        }
    }
}

#[cfg(test)]
mod unit_tests {
    use crate::{client::test_helper::client, editing::FmlEditorError};

    use super::*;

    impl FmlFeatureInspector {
        pub(crate) fn get_first_error(&self, string: String) -> Option<FmlEditorError> {
            let mut errors = self.get_errors(string)?;
            errors.pop()
        }
    }

    #[test]
    fn test_construction() -> Result<()> {
        let client = client("./nimbus_features.yaml", "release")?;
        assert_eq!(
            client.get_feature_ids(),
            vec!["dialog-appearance".to_string()]
        );
        let f = client.get_feature_inspector("dialog-appearance".to_string());
        assert!(f.is_some());

        let f = client.get_feature_inspector("not-there".to_string());
        assert!(f.is_none());

        Ok(())
    }

    #[test]
    fn test_get_first_error_invalid_json() -> Result<()> {
        let client = client("./nimbus_features.yaml", "release")?;
        let f = client
            .get_feature_inspector("dialog-appearance".to_string())
            .unwrap();

        fn test_syntax_error(
            inspector: &FmlFeatureInspector,
            input: &str,
            col: usize,
            highlight: bool,
        ) {
            let error = inspector
                .get_first_error(input.to_string())
                .unwrap_or_else(|| unreachable!("No error for '{input}'"));
            let highlight = if highlight { input } else { "" };
            assert_eq!(
                error,
                syntax_error("Need valid JSON object", 0, col, highlight).unwrap_err()
            );
        }

        test_syntax_error(&f, "", 0, false);
        test_syntax_error(&f, "x", 0, false);
        test_syntax_error(&f, "{ \"\" }, ", 5, false);
        test_syntax_error(&f, "{ \"foo\":", 7, false);

        test_syntax_error(&f, "[]", 0, true);
        test_syntax_error(&f, "1", 0, true);
        test_syntax_error(&f, "true", 0, true);
        test_syntax_error(&f, "\"string\"", 0, true);

        assert!(f.get_first_error("{}".to_string()).is_none());
        Ok(())
    }

    #[test]
    fn test_get_first_error_type_invalid() -> Result<()> {
        let client = client("./nimbus_features.yaml", "release")?;
        let f = client
            .get_feature_inspector("dialog-appearance".to_string())
            .unwrap();

        let s = r#"{}"#;
        assert!(f.get_first_error(s.to_string()).is_none());
        let s = r#"{
            "positive": {}
        }"#;
        assert!(f.get_first_error(s.to_string()).is_none());

        let s = r#"{
            "positive": 1
        }"#;
        if let Some(_err) = f.get_first_error(s.to_string()) {
        } else {
            unreachable!("No error for \"{s}\"");
        }

        let s = r#"{
            "positive1": {}
        }"#;
        if let Some(_err) = f.get_first_error(s.to_string()) {
        } else {
            unreachable!("No error for \"{s}\"");
        }

        Ok(())
    }

    #[test]
    fn test_deterministic_errors() -> Result<()> {
        let client = client("./nimbus_features.yaml", "release")?;
        let inspector = client
            .get_feature_inspector("dialog-appearance".to_string())
            .unwrap();

        let s = r#"{
            "positive": { "yes" : { "trait": 1 }  }
        }"#;
        let err1 = inspector
            .get_first_error(s.to_string())
            .unwrap_or_else(|| unreachable!("No error for \"{s}\""));

        let err2 = inspector
            .get_first_error(s.to_string())
            .unwrap_or_else(|| unreachable!("No error for \"{s}\""));

        assert_eq!(err1, err2);

        Ok(())
    }

    #[test]
    fn test_semantic_errors() -> Result<()> {
        let client = client("./browser.yaml", "release")?;
        let inspector = client
            .get_feature_inspector("nimbus-validation".to_string())
            .unwrap();

        let do_test = |lines: &[&str], token: &str, expected: (u32, u32)| {
            let input = lines.join("\n");
            let err = inspector
                .get_first_error(input.clone())
                .unwrap_or_else(|| unreachable!("No error for \"{input}\""));

            assert_eq!(
                err.highlight,
                Some(token.to_string()),
                "Token {token} not detected in error in {input}"
            );

            let observed = (err.error_span.from.line, err.error_span.from.col);
            assert_eq!(
                expected, observed,
                "Error at {token} in the wrong place in {input}"
            );
        };

        // invalid property name.
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,              // 0
                r#"  "invalid": 1"#, // 1
                r#"}"#,              // 2
            ],
            "\"invalid\"",
            (1, 2),
        );

        // simple type mismatch
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                // 0
                r#"  "icon-type": 1"#, // 1
                r#"}"#,                // 2
            ],
            "1",
            (1, 15),
        );

        // enum mismatch
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                        // 0
                r#"  "icon-type": "invalid""#, // 1
                r#"}"#,                        // 2
            ],
            "\"invalid\"",
            (1, 15),
        );

        // invalid field within object
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                   // 0
                r#"  "nested": {"#,       // 1
                r#"    "invalid": true"#, // 2
                r#"  }"#,                 // 3
                r#"}"#,                   // 4
            ],
            "\"invalid\"",
            (2, 4),
        );

        // nested in an object type mismatch
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                    // 0
                r#"  "nested": {"#,        // 1
                r#"    "is-useful": 256"#, // 2
                r#"  }"#,                  // 3
                r#"}"#,                    // 4
            ],
            "256",
            (2, 17),
        );

        // nested in a map type mismatch
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                      // 0
                r#"  "string-int-map": {"#,  // 1
                r#"    "valid": "invalid""#, // 2
                r#"  }"#,                    // 3
                r#"}"#,                      // 4
            ],
            "\"invalid\"",
            (2, 13),
        );

        // invalid key in enum map
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                 // 0
                r#"  "enum-map": {"#,   // 1
                r#"    "invalid": 42"#, // 2
                r#"  }"#,               // 3
                r#"}"#,                 // 4
            ],
            "\"invalid\"",
            (2, 4),
        );

        // type mismatch in list
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                         // 0
                r#"  "nested-list": ["#,        // 1
                r#"     {"#,                    // 2
                r#"        "is-useful": true"#, // 3
                r#"     },"#,                   // 4
                r#"     false"#,                // 5
                r#"  ]"#,                       // 6
                r#"}"#,                         // 7
            ],
            "false",
            (5, 5),
        );

        // Difficult!
        do_test(
            &[
                // 012345678901234567890
                r#"{"#,                          // 0
                r#"  "string-int-map": {"#,      // 1
                r#"    "nested": 1,"#,           // 2
                r#"    "is-useful": 2,"#,        // 3
                r#"    "invalid": 3"#,           // 4 error is not here!
                r#"  },"#,                       // 5
                r#"  "nested": {"#,              // 6
                r#"    "is-useful": "invalid""#, // 7 error is here!
                r#"  }"#,                        // 8
                r#"}"#,                          // 9
            ],
            "\"invalid\"",
            (7, 17),
        );

        Ok(())
    }
}

#[cfg(test)]
mod correction_candidates {
    use crate::{
        client::test_helper::client,
        editing::{CorrectionCandidate, CursorSpan},
    };

    use super::*;

    // Makes a correction; this is a simulation of what the editor will do.
    fn perform_correction(
        lines: &[&str],
        position: &CursorSpan,
        correction: &CorrectionCandidate,
    ) -> String {
        let position = correction.insertion_span.as_ref().unwrap_or(position);
        position.insert_str(lines, &correction.insert)
    }

    /// Takes an editor input and an inspector.
    /// The editor input (lines) should have exactly one thing wrong with it.
    ///
    /// The correction candidates are tried one by one, and then the lines are
    /// inspected again.
    ///
    /// The function fails if:
    /// a) there are no errors in the initial text
    /// b) there are no completions in the first error.
    /// c) after applying each correction, then there is still an error.
    ///
    /// For obvious reasons, this does not handle arbitrary text. Some text will have too
    /// many errors, some will not have any corrections, and some errors will not be corrected
    /// by every correction (e.g. the key in a feature or object).
    fn try_correcting_single_error(inspector: &FmlFeatureInspector, lines: &[&str]) {
        let input = lines.join("\n");
        let err = inspector.get_first_error(input.clone());
        assert_ne!(None, err, "No error found in input: {input}");
        let err = err.unwrap();
        assert_ne!(
            0,
            err.corrections.len(),
            "No corrections for {input}: {err:?}"
        );

        for correction in &err.corrections {
            let input = perform_correction(lines, &err.error_span, correction);
            let err = inspector.get_first_error(input.clone());
            assert_eq!(None, err, "Error found in {input}");
        }
    }

    #[test]
    fn test_correction_candidates_placeholders_scalar() -> Result<()> {
        let fm = client("./browser.yaml", "release")?;

        let inspector = fm
            .get_feature_inspector("search-term-groups".to_string())
            .unwrap();
        // Correcting a Boolean, should correct 1 to true or false
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,              // 0
                r#"  "enabled": 1"#, // 1
                r#"}"#,              // 2
            ],
        );

        let inspector = fm
            .get_feature_inspector("nimbus-validation".to_string())
            .unwrap();

        // Correcting an Text, should correct 1 to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                           // 0
                r#"  "settings-punctuation": 1"#, // 1
                r#"}"#,                           // 2
            ],
        );

        // Correcting an Image, should correct 1 to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                    // 0
                r#"  "settings-icon": 1"#, // 1
                r#"}"#,                    // 2
            ],
        );

        // Correcting an Int, should correct "not-valid" to 0
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                          // 0
                r#"  "string-int-map": { "#,     // 1
                r#"     "valid": "not-valid" "#, // 2
                r#"   }"#,                       // 3
                r#"}"#,                          // 4
            ],
        );
        Ok(())
    }

    #[test]
    fn test_correction_candidates_replacing_structural() -> Result<()> {
        let fm = client("./browser.yaml", "release")?;
        let inspector = fm
            .get_feature_inspector("nimbus-validation".to_string())
            .unwrap();

        // Correcting an Text, should correct {} to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                            // 0
                r#"  "settings-punctuation": {}"#, // 1
                r#"}"#,                            // 2
            ],
        );

        // Correcting an Text, should correct [] to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                            // 0
                r#"  "settings-punctuation": []"#, // 1
                r#"}"#,                            // 2
            ],
        );

        // Correcting an Text, should correct ["foo"] to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                                 // 0
                r#"  "settings-punctuation": ["foo"]"#, // 1
                r#"}"#,                                 // 2
            ],
        );

        Ok(())
    }

    // All of theses corrections fail because error_path is currently only able
    // to encode the last token as the one in error. If the value in error is a `{ }`, it's encoded
    // as `{}`, which is not found in the source code.
    // The solution is to make error_path keep track of the start token and end token, and calculate
    // an `error_range(src: &src) -> (from: CursorPosition, to: CursorPosition)`.
    // Until that happens, we'll ignore this test.
    #[test]
    fn test_correction_candidates_replacing_structural_plus_whitespace() -> Result<()> {
        let fm = client("./browser.yaml", "release")?;
        let inspector = fm
            .get_feature_inspector("nimbus-validation".to_string())
            .unwrap();

        // Correcting an Text, should correct { } to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                             // 0
                r#"  "settings-punctuation": { }"#, // 1
                r#"}"#,                             // 2
            ],
        );

        // Correcting an Text, should correct [ ] to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                             // 0
                r#"  "settings-punctuation": [ ]"#, // 1
                r#"}"#,                             // 2
            ],
        );

        // Correcting an Text, should correct [ "foo"] to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                                  // 0
                r#"  "settings-punctuation": [ "foo"]"#, // 1
                r#"}"#,                                  // 2
            ],
        );

        Ok(())
    }

    #[test]
    fn test_correction_candidates_placeholders_structural() -> Result<()> {
        let fm = client("./browser.yaml", "release")?;
        let inspector = fm
            .get_feature_inspector("nimbus-validation".to_string())
            .unwrap();

        // Correcting an Option<Text>, should correct true to ""
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                        // 0
                r#"  "settings-title": true"#, // 1
                r#"}"#,                        // 2
            ],
        );

        // Correcting an Map<String, String>, should correct 1 to {}
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                 // 0
                r#"  "string-map": 1"#, // 1
                r#"}"#,                 // 2
            ],
        );

        // Correcting a nested ValidationObject, should correct 1 to {}
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,             // 0
                r#"  "nested": 1"#, // 1
                r#"}"#,             // 2
            ],
        );

        // Correcting a Option<ValidationObject>, should correct 1 to {}
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                      // 0
                r#"  "nested-optional": 1"#, // 1
                r#"}"#,                      // 2
            ],
        );

        // Correcting a List<ValidationObject>, should correct 1 to []
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                  // 0
                r#"  "nested-list": 1"#, // 1
                r#"}"#,                  // 2
            ],
        );

        // Correcting a List<ValidationObject>, should correct 1 to {}
        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,                    // 0
                r#"  "nested-list": [1]"#, // 1
                r#"}"#,                    // 2
            ],
        );

        Ok(())
    }

    #[test]
    fn test_correction_candidates_property_keys() -> Result<()> {
        let fm = client("./browser.yaml", "release")?;
        let inspector = fm.get_feature_inspector("homescreen".to_string()).unwrap();

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890
                r#"{"#,               // 0
                r#"  "invalid": {}"#, // 1
                r#"}"#,               // 2
            ],
        );
        Ok(())
    }

    #[test]
    fn test_correction_candidates_enum_strings() -> Result<()> {
        let fm = client("./enums.fml.yaml", "release")?;
        let inspector = fm
            .get_feature_inspector("my-coverall-feature".to_string())
            .unwrap();

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,                // 0
                r#"  "scalar": true"#, // 1
                r#"}"#,                // 2
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,              // 0
                r#"  "scalar": 13"#, // 1
                r#"}"#,              // 2
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,              // 0
                r#"  "list": [13]"#, // 1
                r#"}"#,              // 2
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,                      // 0
                r#"  "list": ["top", 13 ]"#, // 1
                r#"}"#,                      // 2
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,                   // 0
                r#"  "list": [ false ]"#, // 1
                r#"}"#,                   // 2
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,                         // 0
                r#"  "list": ["top", false ]"#, // 1
                r#"}"#,                         // 2
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,                             // 0
                r#"  "map": { "invalid": false }"#, // 1
                r#"}"#,                             // 2
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{"#,                       // 0
                r#"  "map": { "#,             // 1
                r#"      "top": false, "#,    // 2
                r#"      "invalid": false "#, // 3
                r#"   } "#,                   // 4
                r#"}"#,                       // 5
            ],
        );

        Ok(())
    }

    #[test]
    fn test_correction_candidates_string_aliases() -> Result<()> {
        let fm = client("string-aliases.fml.yaml", "storms")?;
        let inspector = fm
            .get_feature_inspector("my-coverall-team".to_string())
            .unwrap();

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{                    "#, // 0
                r#"  "players": [       "#, // 1
                r#"       "Shrek",      "#, // 2
                r#"       "Fiona"       "#, // 3
                r#"  ],                 "#, // 4
                r#"  "top-player": true "#, // 5
                r#"}"#,                     // 6
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{                       "#, // 0
                r#"  "players": [          "#, // 1
                r#"       "Shrek",         "#, // 2
                r#"       "Fiona"          "#, // 3
                r#"  ],                    "#, // 4
                r#"  "top-player": "Donkey""#, // 5
                r#"}"#,                        // 6
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{                    "#, // 0
                r#"  "players": [       "#, // 1
                r#"       "Shrek",      "#, // 2
                r#"       "Fiona"       "#, // 3
                r#"  ],                 "#, // 4
                r#"  "availability": {  "#, // 5
                r#"     "Donkey": true  "#, // 6
                r#"  }"#,                   // 7
                r#"}"#,                     // 8
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{                    "#, // 0
                r#"  "players": [       "#, // 1
                r#"       "Shrek",      "#, // 2
                r#"       "Fiona"       "#, // 3
                r#"  ],                 "#, // 4
                r#"  "availability": {  "#, // 5
                r#"     "Shrek":   true,"#, // 6
                r#"     "Donkey":  true "#, // 7
                r#"  }"#,                   // 8
                r#"}"#,                     // 9
            ],
        );

        try_correcting_single_error(
            &inspector,
            &[
                // 012345678901234567890123
                r#"{                    "#, // 0
                r#"  "players": [       "#, // 1
                r#"       "Shrek",      "#, // 2
                r#"       "Fiona"       "#, // 3
                r#"  ],                 "#, // 4
                r#"  "availability": {  "#, // 5
                r#"     "Fiona":  true, "#, // 6
                r#"     "invalid": true "#, // 7
                r#"  }"#,                   // 8
                r#"}"#,                     // 9
            ],
        );

        Ok(())
    }
}

#[cfg(test)]
mod config_examples {
    use super::*;
    use crate::client::test_helper::client;

    #[test]
    fn smoke_test() -> Result<()> {
        let fm = client("./config-examples/app.fml.yaml", "release")?;
        let inspector = fm
            .get_feature_inspector(String::from("my-component-feature"))
            .unwrap();

        let examples = inspector.get_examples()?;

        assert_eq!(examples.len(), 5);
        let names: Vec<_> = examples.iter().map(|ex| ex.name.as_str()).collect();
        assert_eq!(
            &[
                "4. Partial example with JSON for imported feature",
                "3. Inlined example for imported feature",
                "2. An example from a file adjacent to the component",
                "1. Inlined example for feature",
                "Default configuration (in full)",
            ],
            names.as_slice()
        );

        Ok(())
    }

    #[test]
    fn validating_test() -> Result<()> {
        let res = client(
            "./config-examples/app-with-broken-example.fml.yaml",
            "release",
        );
        assert!(res.is_err());

        let is_validation_err = matches!(
                res.err().unwrap(),
                FMLError::ValidationError(path, message) if
                       path.as_str() ==
                        "features/my-component-feature#examples[\"Broken example with invalid-property\"]"
                    && message.starts_with(
                        "Invalid property \"invalid-property\""));
        assert!(is_validation_err);

        Ok(())
    }
}
