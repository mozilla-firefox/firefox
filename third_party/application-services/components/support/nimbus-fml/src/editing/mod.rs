/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod cursor_position;
mod error_converter;
mod error_kind;
mod error_path;
mod values_finder;

#[cfg(feature = "client-lib")]
pub(crate) use cursor_position::{CursorPosition, CursorSpan};
pub(crate) use error_converter::ErrorConverter;
pub(crate) use error_kind::ErrorKind;
pub(crate) use error_path::ErrorPath;

pub(crate) struct FeatureValidationError {
    pub(crate) path: ErrorPath,
    pub(crate) kind: ErrorKind,
}

#[cfg(feature = "client-lib")]
#[derive(Debug, PartialEq, Default)]
pub struct FmlEditorError {
    /// The message to display to the user.
    pub message: String,
    /// The token to highlight, and to replace
    pub highlight: Option<String>,
    /// The position in the source code of the first
    /// character of the highlight
    pub error_span: CursorSpan,
    /// The list of possible corrective actions the user
    /// can take to fix this error.
    pub corrections: Vec<CorrectionCandidate>,
    pub line: u32,
    pub col: u32,
}

#[cfg(feature = "client-lib")]
#[derive(Debug, Default, PartialEq)]
pub struct CorrectionCandidate {
    /// The string that should be inserted into the source
    pub insert: String,
    /// The short display name to represent the fix.
    pub display_name: Option<String>,

    /// The span where the `insert` string should overwrite. If None,
    /// then use the `error_span`.
    pub insertion_span: Option<CursorSpan>,

    /// The final position of the cursor after the insertion has taken place.
    /// If None, then should be left to the editor to decide.
    pub cursor_at: Option<CursorPosition>,
}

/// Constructors
#[cfg(feature = "client-lib")]
impl CorrectionCandidate {
    /// Replace the error token with a quoted string.
    /// The display is the unquoted string.
    pub(crate) fn string_replacement(s: &str) -> Self {
        CorrectionCandidate {
            insert: format!("\"{}\"", s),
            display_name: Some(s.to_owned()),
            ..Default::default()
        }
    }

    /// Replace the error token with the literal,
    /// represented by the `s: &str`.
    pub(crate) fn literal_replacement(s: &str) -> Self {
        CorrectionCandidate {
            insert: s.to_owned(),
            ..Default::default()
        }
    }
}
