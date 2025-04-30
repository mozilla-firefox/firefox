/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// On Windows Breakpad uses a named pipe for communication between the
// exception handlers and the crash generator, and Windows UTF-16 strings for
// name and paths, so all we need to pass between processes are
// 2-bytes-per-character strings.

use std::ffi::{OsStr, OsString};

use crate::BreakpadString;

pub type Pid = u32;
pub type BreakpadChar = u16;
pub type AncillaryData = ();
pub type BreakpadRawData = *const u16;

pub struct BreakpadData {
    data: OsString,
}

impl BreakpadData {
    /// Create a new instance of the BreakpadData object from a Windows wide-char nul-terminated string
    ///
    /// # Safety
    ///
    /// `raw` must point to a valid Windows wide-char nul-terminated string
    pub unsafe fn new(raw: BreakpadRawData) -> BreakpadData {
        BreakpadData {
            data: <OsString as BreakpadString>::from_ptr(raw),
        }
    }

    pub fn into_raw(self) -> BreakpadRawData {
        self.data.into_raw()
    }
}

impl AsRef<OsStr> for BreakpadData {
    fn as_ref(&self) -> &OsStr {
        &self.data
    }
}
