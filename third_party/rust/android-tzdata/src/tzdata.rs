//! Logic was mainly ported from https://android.googlesource.com/platform/libcore/+/jb-mr2-release/luni/src/main/java/libcore/util/ZoneInfoDB.java

use core::{cmp::Ordering, convert::TryInto};
use std::{
    fs::File,
    io::{self, ErrorKind, Read, Seek, SeekFrom},
};

// The database uses 32-bit (4 byte) integers.
const TZ_INT_SIZE: usize = 4;
// The first 12 bytes contain a special version string.
const MAGIC_SIZE: usize = 12;
const HEADER_SIZE: usize = MAGIC_SIZE + 3 * TZ_INT_SIZE;
// The database reserves 40 bytes for each id.
const TZ_NAME_SIZE: usize = 40;
const INDEX_ENTRY_SIZE: usize = TZ_NAME_SIZE + 3 * TZ_INT_SIZE;
const TZDATA_LOCATIONS: [TzdataLocation; 2] = [
    TzdataLocation {
        env_var: "ANDROID_DATA",
        path: "/misc/zoneinfo/",
    },
    TzdataLocation {
        env_var: "ANDROID_ROOT",
        path: "/usr/share/zoneinfo/",
    },
];

#[derive(Debug)]
struct TzdataLocation {
    env_var: &'static str,
    path: &'static str,
}

#[derive(Debug, Clone, Copy)]
struct Header {
    index_offset: usize,
    data_offset: usize,
    _zonetab_offset: usize,
}

#[derive(Debug)]
struct Index(Vec<u8>);

#[derive(Debug, Clone, Copy)]
struct IndexEntry<'a> {
    _name: &'a [u8],
    offset: usize,
    length: usize,
    _raw_utc_offset: usize,
}

pub(super) fn find_file() -> Result<File, io::Error> {
    for location in &TZDATA_LOCATIONS {
        if let Ok(env_value) = std::env::var(location.env_var) {
            if let Ok(file) = File::open(format!("{}{}tzdata", env_value, location.path)) {
                return Ok(file);
            }
        }
    }
    Err(io::Error::from(io::ErrorKind::NotFound))
}

pub(super) fn find_tz_data_in_file(
    mut file: impl Read + Seek,
    tz_name: &str,
) -> Result<Vec<u8>, io::Error> {
    let header = Header::new(&mut file)?;
    let index = Index::new(&mut file, header)?;
    if let Some(entry) = index.find_entry(tz_name) {
        file.seek(SeekFrom::Start((entry.offset + header.data_offset) as u64))?;
        let mut tz_data = vec![0u8; entry.length];
        file.read_exact(&mut tz_data)?;
        Ok(tz_data)
    } else {
        Err(io::Error::from(ErrorKind::NotFound))
    }
}

impl Header {
    fn new(mut file: impl Read + Seek) -> Result<Self, io::Error> {
        let mut buf = [0; HEADER_SIZE];
        file.read_exact(&mut buf)?;
        if !buf.starts_with(b"tzdata") || buf[MAGIC_SIZE - 1] != 0u8 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "invalid magic number",
            ));
        }
        Ok(Self {
            index_offset: parse_tz_int(&buf, MAGIC_SIZE) as usize,
            data_offset: parse_tz_int(&buf, MAGIC_SIZE + TZ_INT_SIZE) as usize,
            _zonetab_offset: parse_tz_int(&buf, MAGIC_SIZE + 2 * TZ_INT_SIZE) as usize,
        })
    }
}

impl Index {
    fn new(mut file: impl Read + Seek, header: Header) -> Result<Self, io::Error> {
        file.seek(SeekFrom::Start(header.index_offset as u64))?;
        let size = header.data_offset - header.index_offset;
        let mut bytes = vec![0; size];
        file.read_exact(&mut bytes)?;
        Ok(Self(bytes))
    }

    fn find_entry(&self, name: &str) -> Option<IndexEntry> {
        let name_bytes = name.as_bytes();
        let name_len = name_bytes.len();
        if name_len > TZ_NAME_SIZE {
            return None;
        }

        let zeros = [0u8; TZ_NAME_SIZE];
        let cmp = |chunk: &&[u8]| -> Ordering {
            // tz names always have TZ_NAME_SIZE bytes and are right-padded with 0s
            // so we check that a chunk starts with `name` and the remaining bytes are 0
            chunk[..name_len]
                .cmp(name_bytes)
                .then_with(|| chunk[name_len..TZ_NAME_SIZE].cmp(&zeros[name_len..]))
        };

        let chunks: Vec<_> = self.0.chunks_exact(INDEX_ENTRY_SIZE).collect();
        chunks
            .binary_search_by(cmp)
            .map(|idx| IndexEntry::new(chunks[idx]))
            .ok()
    }
}

impl<'a> IndexEntry<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self {
            _name: bytes[..TZ_NAME_SIZE]
                .splitn(2, |&b| b == 0u8)
                .next()
                .unwrap(),
            offset: parse_tz_int(bytes, TZ_NAME_SIZE) as usize,
            length: parse_tz_int(bytes, TZ_NAME_SIZE + TZ_INT_SIZE) as usize,
            _raw_utc_offset: parse_tz_int(bytes, TZ_NAME_SIZE + 2 * TZ_INT_SIZE) as usize,
        }
    }
}

/// Panics if slice does not contain [TZ_INT_SIZE] bytes beginning at start.
fn parse_tz_int(slice: &[u8], start: usize) -> u32 {
    u32::from_be_bytes(slice[start..start + TZ_INT_SIZE].try_into().unwrap())
}

#[cfg(test)]
mod test {
    use super::*;
    use std::fs::File;
    use std::io::Cursor;

    #[test]
    fn parse() {
        let mut archive = File::open("tests/resources/tzdata.zip").unwrap();
        let mut zip = zip::ZipArchive::new(&mut archive).unwrap();
        let mut file = zip.by_index(0).unwrap();
        let mut data = Vec::new();
        file.read_to_end(&mut data).unwrap();
        let cursor = Cursor::new(data);
        let tz = find_tz_data_in_file(cursor, "Europe/Kiev").unwrap();
        assert!(tz.starts_with(b"TZif2"));
    }
}
