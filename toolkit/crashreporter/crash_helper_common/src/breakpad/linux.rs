/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// On Linux breakpad uses a pipe to communicate with the exception handler, so
// we need to pass a file descriptor between the client and the crash generator
// and regular C strings for paths and names.

use nix::libc::pid_t;
use std::{ffi::c_char, fmt, os::fd::RawFd};

pub type Pid = pid_t;
pub type BreakpadChar = c_char;
pub type AncillaryData = RawFd;
pub type BreakpadRawData = RawFd;

pub struct BreakpadData {
    data: RawFd,
}

impl BreakpadData {
    pub fn new(raw: BreakpadRawData) -> BreakpadData {
        BreakpadData { data: raw }
    }

    pub fn into_raw(self) -> BreakpadRawData {
        self.data
    }
}

impl fmt::Display for BreakpadData {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.data)
    }
}
