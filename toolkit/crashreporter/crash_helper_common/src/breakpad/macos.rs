/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// On macOS breakpad uses a mach port to communicate with the exception
// handler and passes around its name as a string, additionally the crash
// generator uses regular C strings for paths and names.

use nix::libc::pid_t;
use std::{
    ffi::{c_char, OsString},
    fmt,
};

use crate::BreakpadString;

pub type Pid = pid_t;
pub type BreakpadChar = c_char;
pub type AncillaryData = ();
pub type BreakpadRawData = *const c_char;

pub struct BreakpadData {
    data: OsString,
}

impl BreakpadData {
    pub unsafe fn new(raw: BreakpadRawData) -> BreakpadData {
        BreakpadData {
            data: <OsString as BreakpadString>::from_ptr(raw),
        }
    }

    pub fn into_raw(self) -> BreakpadRawData {
        self.data.into_raw()
    }
}

impl fmt::Display for BreakpadData {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{}",
            self.data
                .to_str()
                .expect("Breakpad data must be a valid UTF-8 string")
        )
    }
}
