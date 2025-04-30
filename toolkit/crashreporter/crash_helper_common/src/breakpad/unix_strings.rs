/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::{
    alloc::{alloc, dealloc, Layout},
    ffi::{c_char, OsString},
    mem::align_of,
    os::unix::ffi::OsStringExt,
};

use crate::{errors::MessageError, BreakpadString};

use super::BreakpadChar;

// BreakpadString implementation for regular 8-byte per character strings

impl BreakpadString for OsString {
    fn serialize(&self) -> Vec<u8> {
        <OsString as Clone>::clone(self).into_vec()
    }

    fn deserialize(bytes: &[u8]) -> Result<OsString, MessageError> {
        Ok(OsString::from_vec(bytes.to_owned()))
    }

    unsafe fn from_ptr(ptr: *const BreakpadChar) -> OsString {
        let chars = array_from_c_char_string(ptr);
        OsString::from_vec(chars)
    }

    fn into_raw(self) -> *mut BreakpadChar {
        let chars: Vec<u8> = self.into_vec();
        let layout = Layout::from_size_align(chars.len(), align_of::<u8>())
            .expect("Impossible layout for raw string");
        unsafe {
            let raw_chars = alloc(layout);
            chars
                .as_ptr()
                .copy_to_nonoverlapping(raw_chars, chars.len());
            raw_chars as *mut BreakpadChar
        }
    }

    unsafe fn from_raw(ptr: *mut BreakpadChar) -> OsString {
        let chars = array_from_c_char_string(ptr);
        let layout = Layout::from_size_align(chars.len(), align_of::<u8>())
            .expect("Impossible layout for raw string");
        dealloc(ptr as *mut u8, layout);

        OsString::from_vec(chars)
    }
}

/// Read a nul-terminated C string pointed to by `ptr` and store its
/// characters into an array, including the trailing nul character.
///
/// # Safety
///
/// The `ptr` argument must point to a valid nul-terminated C string.
unsafe fn array_from_c_char_string(ptr: *const c_char) -> Vec<u8> {
    std::ffi::CStr::from_ptr(ptr).to_bytes_with_nul().to_owned()
}
