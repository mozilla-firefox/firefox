/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::{ffi::CString, mem::size_of};

use error::ReadError;

pub mod error;
mod platform;

#[cfg(target_os = "windows")]
pub type ProcessHandle = windows_sys::Win32::Foundation::HANDLE;

#[cfg(any(target_os = "linux", target_os = "android"))]
pub type ProcessHandle = libc::pid_t;

#[cfg(target_os = "macos")]
pub type ProcessHandle = mach2::mach_types::task_t;

pub struct ProcessReader {
    process: ProcessHandle,
}

impl ProcessReader {
    pub fn copy_null_terminated_string(&self, address: usize) -> Result<CString, ReadError> {
        // Try copying the string word-by-word first, this is considerably
        // faster than one byte at a time.
        if let Ok(string) = self.copy_null_terminated_string_word_by_word(address) {
            return Ok(string);
        }

        // Reading the string one word at a time failed, let's try again one
        // byte at a time. It's slow but it might work in situations where the
        // string alignment causes word-by-word access to straddle page
        // boundaries.
        let mut length = 0;
        let mut string = Vec::<u8>::new();

        loop {
            let char = self.copy_object::<u8>(address + length)?;
            length += 1;
            string.push(char);

            if char == 0 {
                break;
            }
        }

        // SAFETY: If we reach this point we've read at least one byte and we
        // know that the last one we read is nul.
        Ok(unsafe { CString::from_vec_with_nul_unchecked(string) })
    }

    fn copy_null_terminated_string_word_by_word(
        &self,
        address: usize,
    ) -> Result<CString, ReadError> {
        const WORD_SIZE: usize = size_of::<usize>();
        let mut length = 0;
        let mut string = Vec::<u8>::new();

        loop {
            let array = self.copy_array::<u8>(address + length, WORD_SIZE)?;
            let null_terminator = array.iter().position(|&e| e == 0);
            length += null_terminator.unwrap_or(WORD_SIZE);
            string.extend(array);

            if null_terminator.is_some() {
                string.truncate(length + 1);
                break;
            }
        }

        // SAFETY: If we reach this point we've read at least one byte and we
        // know that the last one we read is nul.
        Ok(unsafe { CString::from_vec_with_nul_unchecked(string) })
    }
}
