/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! We unfortunately have to mock `Path` because of `exists`, `try_exists`, and `metadata`.

pub use std::path::*;

use super::mock::MockKey;
use std::ffi::{OsStr, OsString};

macro_rules! delegate {
    ( fn $name:ident (&self $(, $arg:ident : $argty:ty )* ) -> $ret:ty ) => {
        pub fn $name (&self, $($arg : $ty)*) -> $ret {
            self.0.$name($($arg),*)
        }
    }
}

#[repr(transparent)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize)]
pub struct Path(std::path::Path);

impl AsRef<std::path::Path> for Path {
    fn as_ref(&self) -> &std::path::Path {
        &self.0
    }
}

impl ToOwned for Path {
    type Owned = PathBuf;
    fn to_owned(&self) -> Self::Owned {
        PathBuf(self.0.to_owned())
    }
}

impl AsRef<OsStr> for Path {
    fn as_ref(&self) -> &OsStr {
        self.0.as_ref()
    }
}

impl AsRef<Path> for &str {
    fn as_ref(&self) -> &Path {
        Path::from_path(self.as_ref())
    }
}

impl AsRef<Path> for String {
    fn as_ref(&self) -> &Path {
        Path::from_path(self.as_ref())
    }
}

impl AsRef<Path> for &OsStr {
    fn as_ref(&self) -> &Path {
        Path::from_path(self.as_ref())
    }
}

impl AsRef<Path> for OsString {
    fn as_ref(&self) -> &Path {
        Path::from_path(self.as_ref())
    }
}

impl Path {
    fn from_path(path: &std::path::Path) -> &Self {
        // # Safety
        // Transparent wrapper is safe to transmute.
        unsafe { std::mem::transmute(path) }
    }

    pub fn new<S: AsRef<OsStr> + ?Sized>(s: &S) -> &Self {
        Self::from_path(std::path::Path::new(s))
    }

    pub fn exists(&self) -> bool {
        super::fs::MockFS
            .try_get(|files| {
                files
                    .path(self, false, |item| match &item.content {
                        super::fs::MockFSContent::File(r) => r.is_ok(),
                        _ => true,
                    })
                    .unwrap_or(false)
            })
            .unwrap_or(false)
    }

    pub fn read_dir(&self) -> super::io::Result<super::fs::ReadDir> {
        super::fs::ReadDir::new(&self.0)
    }

    delegate!(fn display(&self) -> Display<'_>);
    delegate!(fn file_stem(&self) -> Option<&OsStr>);
    delegate!(fn file_name(&self) -> Option<&OsStr>);
    delegate!(fn extension(&self) -> Option<&OsStr>);

    pub fn join<P: AsRef<Path>>(&self, path: P) -> PathBuf {
        PathBuf(self.0.join(&path.as_ref().0))
    }

    pub fn parent(&self) -> Option<&Path> {
        self.0.parent().map(Path::from_path)
    }

    pub fn ancestors(&self) -> Ancestors<'_> {
        Ancestors(self.0.ancestors())
    }
}

#[repr(transparent)]
pub struct Ancestors<'a>(std::path::Ancestors<'a>);

impl<'a> Iterator for Ancestors<'a> {
    type Item = &'a Path;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next().map(Path::from_path)
    }
}

#[repr(transparent)]
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PathBuf(pub(super) std::path::PathBuf);

impl PathBuf {
    pub fn set_extension<S: AsRef<OsStr>>(&mut self, extension: S) -> bool {
        self.0.set_extension(extension)
    }

    pub fn push<P: AsRef<Path>>(&mut self, path: P) {
        self.0.push(path.as_ref())
    }

    pub fn pop(&mut self) -> bool {
        self.0.pop()
    }
}

impl std::ops::Deref for PathBuf {
    type Target = Path;
    fn deref(&self) -> &Self::Target {
        Path::from_path(self.0.as_ref())
    }
}

impl std::borrow::Borrow<Path> for PathBuf {
    fn borrow(&self) -> &Path {
        Path::from_path(self.0.as_ref())
    }
}

impl AsRef<Path> for PathBuf {
    fn as_ref(&self) -> &Path {
        Path::from_path(self.0.as_ref())
    }
}

impl AsRef<std::path::Path> for PathBuf {
    fn as_ref(&self) -> &std::path::Path {
        self.0.as_ref()
    }
}

impl AsRef<OsStr> for PathBuf {
    fn as_ref(&self) -> &OsStr {
        self.0.as_ref()
    }
}

impl From<std::ffi::OsString> for PathBuf {
    fn from(os_str: std::ffi::OsString) -> Self {
        PathBuf(os_str.into())
    }
}

impl From<std::path::PathBuf> for PathBuf {
    fn from(pathbuf: std::path::PathBuf) -> Self {
        PathBuf(pathbuf)
    }
}

impl Into<std::path::PathBuf> for PathBuf {
    fn into(self) -> std::path::PathBuf {
        self.0
    }
}

impl From<PathBuf> for std::ffi::OsString {
    fn from(pathbuf: PathBuf) -> Self {
        pathbuf.0.into()
    }
}

impl From<&str> for PathBuf {
    fn from(s: &str) -> Self {
        PathBuf(s.into())
    }
}

impl From<String> for PathBuf {
    fn from(s: String) -> Self {
        PathBuf(s.into())
    }
}
impl super::mock::MockUnwrap for PathBuf {
    type Inner = std::path::PathBuf;

    fn unwrap(self) -> Self::Inner {
        self.0
    }
}
