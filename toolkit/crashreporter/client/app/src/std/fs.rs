/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::std::mock::{mock_key, try_hook, MockKey};
use std::collections::HashMap;
use std::ffi::OsString;
use std::io::{ErrorKind, Read, Result, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::SystemTime;

/// Mock filesystem file content.
#[derive(Debug, Default, Clone)]
pub struct MockFileContent(Arc<Mutex<Vec<u8>>>);

impl MockFileContent {
    pub fn empty() -> Self {
        Self::default()
    }

    pub fn new(data: String) -> Self {
        Self::new_bytes(data.into())
    }

    pub fn new_bytes(data: Vec<u8>) -> Self {
        MockFileContent(Arc::new(Mutex::new(data)))
    }

    pub fn make_copy(&self) -> Self {
        Self::new_bytes(self.0.lock().unwrap().clone())
    }
}

impl From<()> for MockFileContent {
    fn from(_: ()) -> Self {
        Self::empty()
    }
}

impl From<String> for MockFileContent {
    fn from(s: String) -> Self {
        Self::new(s)
    }
}

impl From<&str> for MockFileContent {
    fn from(s: &str) -> Self {
        Self::new(s.to_owned())
    }
}

impl From<Vec<u8>> for MockFileContent {
    fn from(bytes: Vec<u8>) -> Self {
        Self::new_bytes(bytes)
    }
}

impl From<&[u8]> for MockFileContent {
    fn from(bytes: &[u8]) -> Self {
        Self::new_bytes(bytes.to_owned())
    }
}

/// Mocked filesystem directory entries.
pub type MockDirEntries = HashMap<OsString, MockFSItem>;

/// The content of a mock filesystem item.
pub enum MockFSContent {
    /// File content.
    File(Result<MockFileContent>),
    /// A directory with the given entries.
    Dir(MockDirEntries),
}

impl std::fmt::Debug for MockFSContent {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Self::File(_) => f.debug_tuple("File").finish(),
            Self::Dir(e) => f.debug_tuple("Dir").field(e).finish(),
        }
    }
}

/// A mock filesystem item.
#[derive(Debug)]
pub struct MockFSItem {
    /// The content of the item (file/dir).
    pub content: MockFSContent,
    /// The modification time of the item.
    pub modified: SystemTime,
}

impl From<MockFSContent> for MockFSItem {
    fn from(content: MockFSContent) -> Self {
        MockFSItem {
            content,
            modified: SystemTime::UNIX_EPOCH,
        }
    }
}

/// A mock filesystem.
#[derive(Debug, Clone)]
pub struct MockFiles {
    root: Arc<Mutex<MockFSItem>>,
}

impl Default for MockFiles {
    fn default() -> Self {
        MockFiles {
            root: Arc::new(Mutex::new(MockFSContent::Dir(Default::default()).into())),
        }
    }
}

impl MockFiles {
    /// Create a new, empty filesystem.
    pub fn new() -> Self {
        Self::default()
    }

    /// Add a mocked file with the given content. The modification time will be the unix epoch.
    ///
    /// Pancis if the parent directory is not already mocked.
    pub fn add_file<P: AsRef<Path>, C: Into<MockFileContent>>(&self, path: P, content: C) -> &Self {
        self.add_file_result(path, Ok(content.into()), SystemTime::UNIX_EPOCH)
    }

    /// Add a mocked directory.
    pub fn add_dir<P: AsRef<Path>>(&self, path: P) -> &Self {
        self.path(path, true, |_| ()).unwrap();
        self
    }

    /// Add a mocked file that returns the given result and has the given modification time.
    ///
    /// Pancis if the parent directory is not already mocked.
    pub fn add_file_result<P: AsRef<Path>>(
        &self,
        path: P,
        result: Result<MockFileContent>,
        modified: SystemTime,
    ) -> &Self {
        let name = path.as_ref().file_name().expect("invalid path");
        self.parent_dir(path.as_ref(), move |dir| {
            if dir.contains_key(name) {
                Err(ErrorKind::AlreadyExists.into())
            } else {
                dir.insert(
                    name.to_owned(),
                    MockFSItem {
                        content: MockFSContent::File(result),
                        modified,
                    },
                );
                Ok(())
            }
        })
        .and_then(|r| r)
        .unwrap();
        self
    }

    /// If create_dirs is true, all missing path components (_including the final component_) are
    /// created as directories. In this case `Err` is only returned if a file conflicts with
    /// a directory component.
    pub fn path<P: AsRef<Path>, F, R>(&self, path: P, create_dirs: bool, f: F) -> Result<R>
    where
        F: FnOnce(&mut MockFSItem) -> R,
    {
        let mut guard = self.root.lock().unwrap();
        let mut cur_entry = &mut *guard;
        for component in path.as_ref().components() {
            use std::path::Component::*;
            match component {
                CurDir | RootDir | Prefix(_) => continue,
                ParentDir => panic!("unsupported path: {}", path.as_ref().display()),
                Normal(name) => {
                    let cur_dir = match &mut cur_entry.content {
                        MockFSContent::File(_) => return Err(ErrorKind::NotFound.into()),
                        MockFSContent::Dir(d) => d,
                    };
                    cur_entry = if create_dirs {
                        cur_dir
                            .entry(name.to_owned())
                            .or_insert_with(|| MockFSContent::Dir(Default::default()).into())
                    } else {
                        cur_dir.get_mut(name).ok_or(ErrorKind::NotFound)?
                    };
                }
            }
        }
        Ok(f(cur_entry))
    }

    /// Get the mocked parent directory of the given path and call a callback on the mocked
    /// directory's entries.
    pub fn parent_dir<P: AsRef<Path>, F, R>(&self, path: P, f: F) -> Result<R>
    where
        F: FnOnce(&mut MockDirEntries) -> R,
    {
        self.path(
            path.as_ref().parent().unwrap_or(&Path::new("")),
            false,
            move |item| match &mut item.content {
                MockFSContent::File(_) => Err(ErrorKind::NotFound.into()),
                MockFSContent::Dir(d) => Ok(f(d)),
            },
        )
        .and_then(|r| r)
    }

    /// Return a file assertion helper for the mocked filesystem.
    pub fn assert_files(&self) -> AssertFiles {
        let mut files = HashMap::new();
        let root = self.root.lock().unwrap();

        fn dir(files: &mut HashMap<PathBuf, MockFileContent>, path: &Path, item: &MockFSItem) {
            match &item.content {
                MockFSContent::File(Ok(c)) => {
                    files.insert(path.to_owned(), c.clone());
                }
                MockFSContent::Dir(d) => {
                    for (component, item) in d {
                        dir(files, &path.join(component), item);
                    }
                }
                _ => (),
            }
        }
        dir(&mut files, Path::new(""), &*root);
        AssertFiles { files }
    }
}

/// A utility for asserting the state of the mocked filesystem.
///
/// All files must be accounted for; when dropped, a panic will occur if some files remain which
/// weren't checked.
#[derive(Debug)]
pub struct AssertFiles {
    files: HashMap<PathBuf, MockFileContent>,
}

// On windows we ignore drive prefixes. This is only relevant for real paths, which are only
// present for edge case situations in tests (where AssertFiles is used).
fn remove_prefix(p: &Path) -> &Path {
    let mut iter = p.components();
    if let Some(std::path::Component::Prefix(_)) = iter.next() {
        iter.next(); // Prefix is followed by RootDir
        iter.as_path()
    } else {
        p
    }
}

impl AssertFiles {
    /// Assert that the given path contains the given content (as a utf8 string).
    pub fn check<P: AsRef<Path>, S: AsRef<str>>(&mut self, path: P, content: S) -> &mut Self {
        let p = remove_prefix(path.as_ref());
        let Some(mfc) = self.files.remove(p) else {
            panic!("missing file: {}", p.display());
        };
        let guard = mfc.0.lock().unwrap();
        assert_eq!(
            std::str::from_utf8(&*guard).unwrap(),
            content.as_ref(),
            "file content mismatch: {}",
            p.display()
        );
        self
    }

    /// Assert that the given path contains the given byte content.
    pub fn check_bytes<P: AsRef<Path>, B: AsRef<[u8]>>(
        &mut self,
        path: P,
        content: B,
    ) -> &mut Self {
        let p = remove_prefix(path.as_ref());
        let Some(mfc) = self.files.remove(p) else {
            panic!("missing file: {}", p.display());
        };
        let guard = mfc.0.lock().unwrap();
        assert_eq!(
            &*guard,
            content.as_ref(),
            "file content mismatch: {}",
            p.display()
        );
        self
    }

    /// Ignore the given file (whether it exists or not).
    pub fn ignore<P: AsRef<Path>>(&mut self, path: P) -> &mut Self {
        self.files.remove(remove_prefix(path.as_ref()));
        self
    }

    /// Assert that the given path exists without checking its content.
    pub fn check_exists<P: AsRef<Path>>(&mut self, path: P) -> &mut Self {
        let p = remove_prefix(path.as_ref());
        if self.files.remove(p).is_none() {
            panic!("missing file: {}", p.display());
        }
        self
    }

    /// Finish checking files.
    ///
    /// This panics if all files were not checked.
    ///
    /// This is also called when the value is dropped.
    pub fn finish(&mut self) {
        let files = std::mem::take(&mut self.files);
        if !files.is_empty() {
            panic!("additional files not expected: {:?}", files.keys());
        }
    }
}

impl Drop for AssertFiles {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            self.finish();
        }
    }
}

mock_key! {
    pub struct MockFS => MockFiles
}

pub struct OpenOptions {
    read: bool,
    write: bool,
    truncate: bool,
    create: bool,
}

macro_rules! flag_setter {
    ( $name:ident ) => {
        pub fn $name(&mut self, $name: bool) -> &mut Self {
            self.$name = $name;
            self
        }
    };
}

impl OpenOptions {
    pub fn new() -> Self {
        OpenOptions {
            read: false,
            write: false,
            truncate: false,
            create: false,
        }
    }

    flag_setter!(read);
    flag_setter!(write);
    flag_setter!(truncate);
    flag_setter!(create);

    pub fn open(&self, path: impl AsRef<Path>) -> Result<File> {
        let path = path.as_ref();
        MockFS.get(|files| {
            let name = path.file_name().expect("invalid path");
            files
                .parent_dir(path, move |d| -> Result<File> {
                    let content = if let Some(item) = d.get(name) {
                        match &item.content {
                            MockFSContent::File(result) => {
                                result.as_ref().cloned().map_err(|e| e.kind())
                            }
                            MockFSContent::Dir(_) => Err(ErrorKind::NotFound),
                        }
                    } else if !self.create {
                        Err(ErrorKind::NotFound)
                    } else {
                        let content = MockFileContent::default();
                        d.insert(
                            name.to_owned(),
                            MockFSItem {
                                content: MockFSContent::File(Ok(content.clone())),
                                modified: super::time::SystemTime::now().0,
                            },
                        );
                        Ok(content)
                    }?;
                    let file = File {
                        content,
                        pos: 0,
                        read: self.read,
                        write: self.write,
                    };
                    if self.truncate {
                        file.set_len(0)?;
                    }
                    Ok(file)
                })
                .and_then(|r| r)
        })
    }
}

pub struct File {
    content: MockFileContent,
    pos: usize,
    read: bool,
    write: bool,
}

impl File {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<File> {
        OpenOptions::new().read(true).open(path)
    }

    pub fn create<P: AsRef<Path>>(path: P) -> Result<File> {
        OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(path)
    }

    pub fn set_len(&self, size: u64) -> Result<()> {
        if !self.write {
            return Err(std::io::ErrorKind::PermissionDenied.into());
        }
        let new_len: usize = size
            .try_into()
            .map_err(|_| std::io::ErrorKind::InvalidInput)?;
        self.content.0.lock().unwrap().resize(new_len, 0);
        Ok(())
    }
}

impl Read for File {
    fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
        if !self.read {
            return Err(std::io::ErrorKind::PermissionDenied.into());
        }
        let guard = self.content.0.lock().unwrap();
        if self.pos >= guard.len() {
            return Ok(0);
        }
        let to_read = std::cmp::min(buf.len(), guard.len() - self.pos);
        buf[..to_read].copy_from_slice(&guard[self.pos..self.pos + to_read]);
        self.pos += to_read;
        Ok(to_read)
    }
}

impl Seek for File {
    fn seek(&mut self, pos: SeekFrom) -> Result<u64> {
        let len = self.content.0.lock().unwrap().len();
        match pos {
            SeekFrom::Start(n) => self.pos = n as usize,
            SeekFrom::End(n) => {
                if n < 0 {
                    let offset = -n as usize;
                    if offset > len {
                        return Err(std::io::Error::new(
                            std::io::ErrorKind::InvalidInput,
                            "out of bounds",
                        ));
                    }
                    self.pos = len - offset;
                } else {
                    self.pos = len + n as usize
                }
            }
            SeekFrom::Current(n) => {
                if n < 0 {
                    let offset = -n as usize;
                    if offset > self.pos {
                        return Err(std::io::Error::new(
                            std::io::ErrorKind::InvalidInput,
                            "out of bounds",
                        ));
                    }
                    self.pos -= offset;
                } else {
                    self.pos += n as usize;
                }
            }
        }
        Ok(self.pos as u64)
    }
}

impl Write for File {
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        if !self.write {
            return Err(std::io::ErrorKind::PermissionDenied.into());
        }
        let mut guard = self.content.0.lock().unwrap();
        let end = self.pos + buf.len();
        if end > guard.len() {
            guard.resize(end, 0);
        }
        (&mut guard[self.pos..end]).copy_from_slice(buf);
        self.pos = end;
        Ok(buf.len())
    }

    fn flush(&mut self) -> Result<()> {
        if !self.write {
            return Err(std::io::ErrorKind::PermissionDenied.into());
        }
        Ok(())
    }
}

pub fn create_dir_all<P: AsRef<Path>>(path: P) -> Result<()> {
    MockFS.get(move |files| files.path(path, true, |_| ()))
}

pub fn rename<P: AsRef<Path>, Q: AsRef<Path>>(from: P, to: Q) -> Result<()> {
    if try_hook(false, "rename_fail") {
        log::debug!("intentionally failing std::fs::rename");
        return Err(ErrorKind::Unsupported.into());
    }

    MockFS.get(move |files| {
        let from_name = from.as_ref().file_name().expect("invalid path");
        let item = files
            .parent_dir(from.as_ref(), move |d| {
                d.remove(from_name).ok_or(ErrorKind::NotFound.into())
            })
            .and_then(|r| r)?;

        let to_name = to.as_ref().file_name().expect("invalid path");
        files
            .parent_dir(to.as_ref(), move |d| {
                // Just error if `to` exists, which doesn't quite follow `std::fs::rename` behavior.
                if d.contains_key(to_name) {
                    Err(ErrorKind::AlreadyExists.into())
                } else {
                    d.insert(to_name.to_owned(), item);
                    Ok(())
                }
            })
            .and_then(|r| r)
    })
}

pub fn copy<P: AsRef<Path>, Q: AsRef<Path>>(from: P, to: Q) -> Result<()> {
    MockFS.get(move |files| {
        let from_name = from.as_ref().file_name().expect("invalid path");
        let item = files
            .parent_dir(from.as_ref(), move |d| {
                d.get(from_name)
                    .ok_or(ErrorKind::NotFound.into())
                    .and_then(|e| match &e.content {
                        MockFSContent::Dir(_) => Err(ErrorKind::NotFound.into()),
                        MockFSContent::File(f) => f
                            .as_ref()
                            .map(|f| MockFSItem {
                                content: MockFSContent::File(Ok(f.make_copy())),
                                modified: e.modified.clone(),
                            })
                            .map_err(|e| e.kind().into()),
                    })
            })
            .and_then(|r| r)?;

        let to_name = to.as_ref().file_name().expect("invalid path");
        files
            .parent_dir(to.as_ref(), move |d| {
                d.insert(to_name.to_owned(), item);
                Ok(())
            })
            .and_then(|r| r)
    })
}

pub fn remove_file<P: AsRef<Path>>(path: P) -> Result<()> {
    MockFS.get(move |files| {
        let name = path.as_ref().file_name().expect("invalid path");
        files
            .parent_dir(path.as_ref(), |d| {
                if let Some(MockFSItem {
                    content: MockFSContent::Dir(_),
                    ..
                }) = d.get(name)
                {
                    Err(ErrorKind::NotFound.into())
                } else {
                    d.remove(name).ok_or(ErrorKind::NotFound.into()).map(|_| ())
                }
            })
            .and_then(|r| r)
    })
}

pub fn read_to_string<P: AsRef<Path>>(path: P) -> Result<String> {
    let mut s = String::new();
    File::open(path.as_ref())?.read_to_string(&mut s)?;
    Ok(s)
}

pub fn write<P: AsRef<Path>, C: AsRef<[u8]>>(path: P, contents: C) -> Result<()> {
    File::create(path.as_ref())?.write_all(contents.as_ref())
}

pub fn read_dir<P: AsRef<crate::std::path::Path>>(path: P) -> Result<ReadDir> {
    ReadDir::new(path.as_ref().as_ref())
}

pub struct ReadDir {
    base: PathBuf,
    children: Vec<OsString>,
}

impl ReadDir {
    pub fn new(path: &Path) -> Result<Self> {
        MockFS.get(move |files| {
            files
                .path(path, false, |item| match &item.content {
                    MockFSContent::Dir(d) => Ok(ReadDir {
                        base: path.to_owned(),
                        children: d.keys().cloned().collect(),
                    }),
                    MockFSContent::File(_) => Err(ErrorKind::NotFound.into()),
                })
                .and_then(|r| r)
        })
    }
}

impl Iterator for ReadDir {
    type Item = Result<DirEntry>;
    fn next(&mut self) -> Option<Self::Item> {
        let child = self.children.pop()?;
        Some(Ok(DirEntry(self.base.join(child))))
    }
}

pub struct DirEntry(PathBuf);

impl DirEntry {
    pub fn path(&self) -> super::path::PathBuf {
        super::path::PathBuf(self.0.clone())
    }

    pub fn metadata(&self) -> Result<Metadata> {
        MockFS.get(|files| {
            files.path(&self.0, false, |item| {
                let is_dir = matches!(&item.content, MockFSContent::Dir(_));
                Metadata {
                    is_dir,
                    modified: item.modified,
                }
            })
        })
    }

    pub fn file_type(&self) -> Result<FileType> {
        Ok(FileType::new(&self.metadata()?))
    }
}

pub struct FileType {
    is_dir: bool,
}

impl FileType {
    fn new(meta: &Metadata) -> Self {
        Self {
            is_dir: meta.is_dir,
        }
    }

    pub fn is_dir(&self) -> bool {
        self.is_dir
    }

    pub fn is_file(&self) -> bool {
        !self.is_dir
    }

    pub fn is_symlink(&self) -> bool {
        false
    }
}

pub struct Metadata {
    is_dir: bool,
    modified: SystemTime,
}

impl Metadata {
    pub fn is_file(&self) -> bool {
        !self.is_dir
    }

    pub fn is_dir(&self) -> bool {
        self.is_dir
    }

    pub fn modified(&self) -> Result<super::time::SystemTime> {
        Ok(super::time::SystemTime(self.modified))
    }
}
