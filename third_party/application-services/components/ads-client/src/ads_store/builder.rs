/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::connection_initializer::AdsStoreConnectionInitializer;
use crate::ads_store::store::AdsStoreHolder;
use crate::ads_store::AdsStore;
use crate::bytesize::ByteSize;
use crate::telemetry;
use crate::IN_MEMORY_DB_PATH;
use rusqlite::OpenFlags;
use sql_support::{open_database, LazyDb};
use std::path::{Path, PathBuf};

const DEFAULT_MAX_SIZE: ByteSize = ByteSize::mib(10);
const MIN_STORE_SIZE: ByteSize = ByteSize::kib(1);
const MAX_STORE_SIZE: ByteSize = ByteSize::mib(100);

#[derive(Debug, thiserror::Error)]
pub enum AdsStoreBuilderError {
    #[error("Database error: {0}")]
    Database(#[from] open_database::Error),
    // TODO: Remove this once memory dbs confirmed to be OK.
    #[error("Database (memory) error: {0}")]
    DatabaseMemory(open_database::Error),
    #[error("Database path cannot be empty")]
    EmptyDbPath,
    #[error(
        "Maximum store size must be between {min_size} and {max_size}, got {size_bytes} bytes"
    )]
    InvalidMaxSize {
        max_size: String,
        min_size: String,
        size_bytes: u64,
    },
}

pub struct AdsStoreBuilder {
    db_path: Option<PathBuf>,
    max_size: Option<ByteSize>,
}

impl AdsStoreBuilder {
    /// Create a new AdsStoreBuilder. Concludes with 'build': `AdsStoreBuilder::new().build()`
    /// This defaults to an in-memory database, unless `.db_path("some_path.db")` is chained.
    pub fn new() -> Self {
        Self {
            db_path: None,
            max_size: None,
        }
    }

    pub fn build(&self) -> Result<AdsStore, AdsStoreBuilderError> {
        self.validate()?;

        // Fall back to a memory db if the file db fails to open.
        let db = self.open_db()?;
        let holder = AdsStoreHolder::new(db);
        let max_size = self.max_size.unwrap_or(DEFAULT_MAX_SIZE);
        Ok(AdsStore { holder, max_size })
    }

    // `db_path`: location of sqlite file for file-based db. If `db_path()` is not called, the db will be created in-memory.
    pub fn db_path(mut self, db_path: impl Into<PathBuf>) -> Self {
        self.db_path = Some(db_path.into());
        self
    }

    pub fn max_size(mut self, max_size: ByteSize) -> Self {
        self.max_size = Some(max_size);
        self
    }

    // Opens the db at `db_path`, or a memory db if there is no path or it fails to open.
    fn open_db(&self) -> Result<LazyDb<AdsStoreConnectionInitializer>, AdsStoreBuilderError> {
        if let Some(db_path) = &self.db_path {
            match open_connection(db_path) {
                Ok(db) => return Ok(db),
                Err(e) => telemetry::record_ads_store_error(&AdsStoreBuilderError::from(e)),
            }
        }
        // TODO: This is cast to DatabaseMemory rather than Database. Remove this when DatabaseMemory is removed.
        open_connection(Path::new(IN_MEMORY_DB_PATH)).map_err(AdsStoreBuilderError::DatabaseMemory)
    }

    fn validate(&self) -> Result<(), AdsStoreBuilderError> {
        if self
            .db_path
            .as_ref()
            .is_some_and(|p| p.to_string_lossy().trim().is_empty())
        {
            return Err(AdsStoreBuilderError::EmptyDbPath);
        }

        if let Some(max_size) = self.max_size {
            if max_size < MIN_STORE_SIZE || max_size > MAX_STORE_SIZE {
                return Err(AdsStoreBuilderError::InvalidMaxSize {
                    max_size: MAX_STORE_SIZE.to_string(),
                    min_size: MIN_STORE_SIZE.to_string(),
                    size_bytes: max_size.as_u64(),
                });
            }
        }

        Ok(())
    }
}

// Opens the db at `path`, eagerly opening the initial connection so failures surface here.
fn open_connection(
    path: &Path,
) -> Result<LazyDb<AdsStoreConnectionInitializer>, open_database::Error> {
    let db = LazyDb::new(path, OpenFlags::default(), AdsStoreConnectionInitializer {});
    drop(db.lock()?);
    Ok(db)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_test_builder(path: &str) -> AdsStoreBuilder {
        AdsStoreBuilder::new().db_path(path)
    }

    #[test]
    fn test_store_builder_with_defaults() {
        let builder = make_test_builder(IN_MEMORY_DB_PATH);
        assert_eq!(builder.db_path, Some(PathBuf::from(IN_MEMORY_DB_PATH)));
        assert_eq!(builder.max_size, None);
        assert!(builder.build().is_ok());
    }

    #[test]
    fn test_unopenable_path_falls_back_to_memory_db() {
        // The fallback records telemetry, so don't race the telemetry tests.
        let _lock = crate::telemetry::test_lock();
        let store = make_test_builder("this/is/a/test/bad/path")
            .build()
            .unwrap();
        assert!(store.is_memory().unwrap());
    }

    #[test]
    fn test_cache_builder_valid_custom() {
        let builder = make_test_builder(IN_MEMORY_DB_PATH).max_size(ByteSize::b(1024));

        assert_eq!(builder.db_path, Some(PathBuf::from(IN_MEMORY_DB_PATH)));
        assert_eq!(builder.max_size, Some(ByteSize::b(1024)));
        assert!(builder.build().is_ok());
    }

    #[test]
    fn test_validation_empty_db_path() {
        let result = make_test_builder("   ").build();
        assert!(matches!(result, Err(AdsStoreBuilderError::EmptyDbPath)));
    }

    #[test]
    fn test_validation_max_size_too_small() {
        let result = make_test_builder(IN_MEMORY_DB_PATH)
            .max_size(ByteSize::b(512))
            .build();
        assert!(matches!(
            result,
            Err(AdsStoreBuilderError::InvalidMaxSize {
                size_bytes: 512,
                min_size: _,
                max_size: _,
            })
        ));
    }

    #[test]
    fn test_validation_max_size_too_large() {
        let result = make_test_builder(IN_MEMORY_DB_PATH)
            .max_size(ByteSize::b(2 * 1024 * 1024 * 1024))
            .build();
        assert!(matches!(
            result,
            Err(AdsStoreBuilderError::InvalidMaxSize {
                size_bytes: 2147483648,
                min_size: _,
                max_size: _,
            })
        ));
    }

    #[test]
    fn test_validation_max_size_boundaries() {
        let builder_min = make_test_builder(IN_MEMORY_DB_PATH).max_size(MIN_STORE_SIZE);
        assert!(builder_min.build().is_ok());

        let builder_max = make_test_builder(IN_MEMORY_DB_PATH).max_size(MAX_STORE_SIZE);
        assert!(builder_max.build().is_ok());
    }
}
