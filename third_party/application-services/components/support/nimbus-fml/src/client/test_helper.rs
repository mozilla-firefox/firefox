/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::{
    editing::{CursorPosition, CursorSpan},
    error::Result,
    intermediate_representation::FeatureManifest,
    FmlClient,
};

pub(crate) fn client(path: &str, channel: &str) -> Result<FmlClient> {
    let root = env!("CARGO_MANIFEST_DIR");
    let fixtures = format!("{root}/fixtures/fe");
    FmlClient::new_with_ref(
        format!("@my/fixtures/{path}"),
        channel.to_string(),
        Some(fixtures),
    )
}

impl From<FeatureManifest> for FmlClient {
    fn from(manifest: FeatureManifest) -> Self {
        Self::new_from_manifest(manifest)
    }
}

impl CursorPosition {
    /// Translate the (line, col) into an character index into the
    /// given lines.
    ///
    /// This is a test method, so we don't do any normal bounds checking.
    fn offset_in_lines(&self, lines: &[&str]) -> usize {
        let mut offset = self.col as usize;
        let num_lines = self.line as usize;

        for line in lines.iter().take(num_lines) {
            offset += line.chars().count();
        }
        // Adding the num_lines assuming we have one character per newline.
        offset + num_lines
    }
}

impl CursorSpan {
    /// Insert the given string into the given lines.
    ///
    /// This is a test method, so we prefer simplicity of implementation over
    /// performance, memory complexity or robustness.
    pub(crate) fn insert_str(&self, lines: &[&str], inserted: &str) -> String {
        let from = self.from.offset_in_lines(lines);
        let to = self.to.offset_in_lines(lines);
        let src = lines.join("\n");
        let src = src.as_str();
        let start = &src[0..from];
        let end = &src[to..];
        format!("{start}{inserted}{end}")
    }
}

#[cfg(test)]
mod string_manipulation {
    use super::*;

    #[test]
    fn test_offset() {
        let lines = &["0123456789", "0123456789", "0123456789"];
        let pos = CursorPosition::new(0, 0);
        assert_eq!(0, pos.offset_in_lines(lines));

        let pos = CursorPosition::new(1, 0);
        assert_eq!(11, pos.offset_in_lines(lines));

        let pos = CursorPosition::new(2, 0);
        assert_eq!(22, pos.offset_in_lines(lines));

        let pos = CursorPosition::new(0, 5);
        assert_eq!(5, pos.offset_in_lines(lines));
    }

    #[test]
    fn test_insertion() {
        let lines = &["01234", "01234"];

        // Insert.
        let span = CursorSpan {
            from: CursorPosition::new(0, 0),
            to: CursorPosition::new(0, 0),
        };
        assert_eq!(
            String::from("__abc__01234\n01234"),
            span.insert_str(lines, "__abc__")
        );

        // Overwrite
        let span = CursorSpan {
            from: CursorPosition::new(0, 0),
            to: CursorPosition::new(0, 3),
        };
        assert_eq!(
            String::from("__abc__34\n01234"),
            span.insert_str(lines, "__abc__")
        );

        // Overwrite over a line break.
        let span = CursorSpan {
            from: CursorPosition::new(0, 5),
            to: CursorPosition::new(1, 0),
        };
        assert_eq!(
            String::from("01234__abc__01234"),
            span.insert_str(lines, "__abc__")
        );
    }
}
