/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{borrow::Borrow, io, path::PathBuf};

/// A representation of a path as a wide character sequence that can
/// contain unpaired surrogates.
///
/// A [`WidePathBuf`] can be created from an [`nsstring::nsAString`],
/// and copied into an `[nsstring::nsString]`, without re-encoding.
/// Unlike `ns{A}String`, [`WidePathBuf`] implements the built-in
/// comparison and hashing traits, so it can be used as a key in
/// a `HashMap` or a `BTreeMap`.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialOrd, PartialEq)]
pub struct WidePathBuf(Vec<u16>);

impl WidePathBuf {
    /// Creates a [`WidePathBuf`] from a wide character sequence.
    pub fn new(wide: impl AsRef<[u16]>) -> Self {
        Self(wide.as_ref().into())
    }

    /// Returns the wide character sequence in this [`WidePathBuf`].
    pub fn as_wide(&self) -> &[u16] {
        &self.0
    }

    /// Returns the canonical, absolute form of a path, normalizing
    /// `/./` and `/../` components, symbolic links, and
    /// {drive, directory}-relative paths.
    ///
    /// ### Notes
    ///
    /// This function calls [`std::fs::canonicalize`] on Windows, and
    /// reimplements [`std::fs::canonicalize`] on Unix platforms
    /// to work around an [allocator crash][1].
    ///
    /// [1]: https://bugzilla.mozilla.org/show_bug.cgi?id=1531887
    pub fn canonicalize(&self) -> io::Result<PathBuf> {
        #[cfg(windows)]
        {
            use std::{ffi::OsString, os::windows::prelude::*};
            std::fs::canonicalize(OsString::from_wide(&self.0))
        }
        #[cfg(unix)]
        {
            use std::{
                ffi::{CStr, CString, OsString},
                os::unix::prelude::*,
            };
            let path = CString::new(String::from_utf16(&self.0).map_err(io::Error::other)?)?;
            let mut bytes = [0 as libc::c_char; libc::PATH_MAX as usize];
            let ptr = unsafe { libc::realpath(path.as_ptr(), bytes[..].as_mut_ptr()) };
            if ptr.is_null() {
                return Err(io::Error::last_os_error());
            }
            Ok(OsString::from_vec(unsafe { CStr::from_ptr(ptr) }.to_bytes().into()).into())
        }
        #[cfg(all(not(unix), not(windows)))]
        compile_error!("`WidePathBuf::canonicalize` requires Windows or Unix")
    }
}

// Convenience implementation for comparing a [`WidePathBuf`] to a Rust
// `String` or `str`.
impl<T> PartialEq<T> for WidePathBuf
where
    T: Borrow<str>,
{
    fn eq(&self, other: &T) -> bool {
        other
            .borrow()
            .encode_utf16()
            .eq(self.as_wide().iter().copied())
    }
}
