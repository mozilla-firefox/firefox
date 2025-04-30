/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::{
    alloc::{alloc, dealloc, Layout},
    ffi::OsString,
    mem::{align_of, size_of},
    os::windows::ffi::{OsStrExt, OsStringExt},
};

use crate::{errors::MessageError, BreakpadChar, BreakpadString};

// BreakpadString trait implementation for Windows native UTF-16 strings
impl BreakpadString for OsString {
    fn serialize(&self) -> Vec<u8> {
        self.encode_wide().flat_map(|c| c.to_ne_bytes()).collect()
    }

    fn deserialize(bytes: &[u8]) -> Result<OsString, MessageError> {
        if (bytes.len() % 2) != 0 {
            return Err(MessageError::InvalidData);
        }

        let wchars: Vec<u16> = bytes
            .chunks_exact(2)
            .map(|chunk| {
                // SAFETY: We're splitting into exact 2 bytes chunks
                let chunk: [u8; 2] = unsafe { chunk.try_into().unwrap_unchecked() };
                u16::from_ne_bytes(chunk)
            })
            .collect();

        Ok(OsString::from_wide(&wchars))
    }

    unsafe fn from_ptr(ptr: *const BreakpadChar) -> OsString {
        let wchars = array_from_win_string(ptr);
        OsString::from_wide(&wchars)
    }

    fn into_raw(self) -> *mut BreakpadChar {
        let wide_chars: Vec<u16> = self.encode_wide().chain(std::iter::once(0)).collect();
        let layout =
            Layout::from_size_align((wide_chars.len() + 1) * size_of::<u16>(), align_of::<u16>())
                .expect("Impossible layout for raw Windows string");
        unsafe {
            let raw_chars = alloc(layout) as *mut u16;
            wide_chars
                .as_ptr()
                .copy_to_nonoverlapping(raw_chars, wide_chars.len());

            raw_chars
        }
    }

    unsafe fn from_raw(ptr: *mut BreakpadChar) -> OsString {
        let wide_chars = array_from_win_string(ptr);
        let layout =
            Layout::from_size_align((wide_chars.len() + 1) * size_of::<u16>(), align_of::<u16>())
                .expect("Impossible layout for raw Windows string");
        dealloc(ptr as *mut u8, layout);

        OsString::from_wide(&wide_chars)
    }
}

// Read a Windows wide string pointed to by `ptr` and store its characters into
// an array, excluding the trailing nul character.
fn array_from_win_string(ptr: *const u16) -> Vec<u16> {
    let mut wide_chars = Vec::<BreakpadChar>::new();
    let mut ptr = ptr;

    loop {
        let c = unsafe { ptr.read() };

        if c == 0 {
            break;
        }

        wide_chars.push(c);
        ptr = unsafe { ptr.offset(1) };
    }

    wide_chars
}
