/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ops::Add;

use icu_segmenter::GraphemeClusterSegmenter;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct CursorPosition {
    pub line: u32,
    pub col: u32,
}

#[allow(dead_code)]
impl CursorPosition {
    pub(crate) fn new(line: usize, col: usize) -> Self {
        Self {
            line: line as u32,
            col: col as u32,
        }
    }
}

impl From<(usize, usize)> for CursorPosition {
    fn from((line, col): (usize, usize)) -> Self {
        Self::new(line, col)
    }
}

/// CursorSpan is used to for reporting errors and defining corrections.
/// This is passed across the FFI and used by the editor.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct CursorSpan {
    pub from: CursorPosition,
    pub to: CursorPosition,
}

/// CursorPosition + &str -> CursorSpan.
/// This is a convenient way of making CursorSpans.
impl Add<&str> for CursorPosition {
    type Output = CursorSpan;

    fn add(self, rhs: &str) -> Self::Output {
        let mut line_count = 0;
        let mut last_line = None;

        for line in rhs.lines() {
            line_count += 1;
            last_line = Some(line);
        }

        if rhs.ends_with('\n') {
            line_count += 1;
            last_line = None;
        }

        let last_line_length = match last_line {
            Some(line) => {
                let segmenter = GraphemeClusterSegmenter::new();
                let boundary_count = segmenter.segment_str(line).count();
                // segment_str returns N+1 boundaries for N graphemes, so subtract 1
                if boundary_count > 0 {
                    boundary_count - 1
                } else {
                    0
                }
            }
            None => 0,
        } as u32;

        let to = match line_count {
            0 => self.clone(),
            1 => CursorPosition {
                line: self.line,
                col: self.col + last_line_length,
            },
            _ => CursorPosition {
                line: self.line + line_count - 1,
                col: last_line_length,
            },
        };

        Self::Output { from: self, to }
    }
}

impl Add<CursorSpan> for CursorPosition {
    type Output = CursorSpan;

    fn add(self, rhs: CursorSpan) -> Self::Output {
        Self::Output {
            from: self,
            to: rhs.to,
        }
    }
}

#[cfg(test)]
mod unit_tests {
    use super::*;

    #[test]
    fn test_add_str() {
        // start at (10, 10) so we can check that things are zeroing properly.
        let from = CursorPosition::new(10, 10);

        assert_eq!(CursorPosition::new(10, 10), (from.clone() + "").to);
        assert_eq!(CursorPosition::new(10, 11), (from.clone() + "0").to);
        assert_eq!(CursorPosition::new(10, 12), (from.clone() + "01").to);

        assert_eq!(CursorPosition::new(11, 1), (from.clone() + "\n0").to);
        assert_eq!(CursorPosition::new(12, 1), (from.clone() + "\n\n0").to);

        assert_eq!(CursorPosition::new(11, 0), (from.clone() + "\n").to);
        assert_eq!(CursorPosition::new(12, 0), (from.clone() + "\n\n").to);
    }
}
