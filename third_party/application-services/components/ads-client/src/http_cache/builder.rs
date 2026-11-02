/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::connection_initializer::HttpCacheConnectionInitializer;
use super::store::HttpCacheStore;
use crate::bytesize::ByteSize;
use crate::http_cache::HttpCache;
use crate::telemetry;
use rusqlite::OpenFlags;
use sql_support::{open_database, LazyDb};
use std::path::{Path, PathBuf};
use std::time::Duration;

const DEFAULT_MAX_SIZE: ByteSize = ByteSize::mib(10);
const DEFAULT_TTL: Duration = Duration::from_secs(300);

const MIN_CACHE_SIZE: ByteSize = ByteSize::kib(1);
const MAX_CACHE_SIZE: ByteSize = ByteSize::mib(100);
const MIN_TTL: Duration = Duration::from_secs(1);
const MAX_TTL: Duration = Duration::from_secs(60 * 60 * 24 * 7); // 7 days

const IN_MEMORY_DB_PATH: &str = ":memory:";

#[derive(Debug, thiserror::Error)]
pub enum HttpCacheBuilderError {
    #[error("Database error: {0}")]
    Database(#[from] open_database::Error),
    // TODO: Remove this once memory dbs confirmed to be OK.
    #[error("Database (memory) error: {0}")]
    DatabaseMemory(open_database::Error),
    #[error("Database path cannot be empty")]
    EmptyDbPath,
    #[error(
        "Maximum cache size must be between {min_size} and {max_size}, got {size_bytes} bytes"
    )]
    InvalidMaxSize {
        max_size: String,
        min_size: String,
        size_bytes: u64,
    },
    #[error("TTL must be between {min_ttl} and {max_ttl}, got {ttl} seconds")]
    InvalidTtl {
        max_ttl: String,
        min_ttl: String,
        ttl: u64,
    },
}

pub struct HttpCacheBuilder {
    db_path: Option<PathBuf>,
    default_ttl: Option<Duration>,
    max_size: Option<ByteSize>,
    // Flag for whether this db is built in-memory
    // This is set by an error in `open_connection`, or with `build_memory`.
    is_memory: bool,
}

impl HttpCacheBuilder {
    // `db_path`: location of sqlite file for file-based db. If `None`, the db will be created in-memory.
    pub fn new(db_path: Option<impl Into<PathBuf>>) -> Self {
        Self {
            db_path: db_path.map(|x| x.into()),
            default_ttl: None,
            max_size: None,
            is_memory: false,
        }
    }

    // Attempt to build the HttpCache. If a file path exists and can be built successfully, we build
    // a FS sqlite database. Otherwise, we fall back to memory.
    pub fn build(&mut self) -> Result<HttpCache, HttpCacheBuilderError> {
        self.validate()?;

        let db = self.open_connection()?;
        let max_size = self.max_size.unwrap_or(DEFAULT_MAX_SIZE);
        let store = HttpCacheStore::new(db);
        let default_ttl = self.default_ttl.unwrap_or(DEFAULT_TTL);

        Ok(HttpCache {
            default_ttl,
            max_size,
            store,
            is_memory: self.is_memory,
        })
    }

    #[cfg(test)]
    pub fn build_for_time_dependent_tests(&mut self) -> Result<HttpCache, HttpCacheBuilderError> {
        self.validate()?;

        let db = self.open_connection()?;
        let max_size = self.max_size.unwrap_or(DEFAULT_MAX_SIZE);
        let store = HttpCacheStore::new_with_test_clock(db);
        let default_ttl = self.default_ttl.unwrap_or(DEFAULT_TTL);

        Ok(HttpCache {
            default_ttl,
            max_size,
            store,
            is_memory: self.is_memory,
        })
    }

    pub fn default_ttl(mut self, ttl: Duration) -> Self {
        self.default_ttl = Some(ttl);
        self
    }

    pub fn max_size(mut self, max_size: ByteSize) -> Self {
        self.max_size = Some(max_size);
        self
    }

    fn open_connection(
        &mut self,
    ) -> Result<LazyDb<HttpCacheConnectionInitializer>, HttpCacheBuilderError> {
        if !cfg!(test) && !self.is_memory {
            if let Some(db_path) = &self.db_path {
                let db = LazyDb::new(
                    db_path,
                    OpenFlags::default(),
                    HttpCacheConnectionInitializer {},
                );

                // Attempt to open initial connection, resolving an error if needed.
                let conn = db.lock();
                if let Some(e) = conn.err() {
                    telemetry::record_build_cache_error(&HttpCacheBuilderError::from(e));
                } else {
                    return Ok(db);
                }
            }
        }

        // If we cannot instantiate a filesystem db, cfg!(test) == true, or is_memory == true, we fall back to a memory db.
        // We set is_memory to true in case it was not already.
        let memory_db = LazyDb::new(
            Path::new(IN_MEMORY_DB_PATH),
            OpenFlags::default(),
            HttpCacheConnectionInitializer {},
        );
        self.is_memory = true;

        // Attempt to open initial connection, resolving an error if needed.
        // TODO: This is cast to DatabaseMemory rather than Database. Remove this when DatabaseMemory is removed.
        let _ = memory_db
            .lock()
            .map_err(HttpCacheBuilderError::DatabaseMemory)?;

        Ok(memory_db)
    }

    fn validate(&self) -> Result<(), HttpCacheBuilderError> {
        if self
            .db_path
            .as_ref()
            .map(|p| p.to_string_lossy().trim().is_empty())
            .unwrap_or(false)
        {
            return Err(HttpCacheBuilderError::EmptyDbPath);
        }

        if let Some(max_size) = self.max_size {
            if max_size < MIN_CACHE_SIZE || max_size > MAX_CACHE_SIZE {
                return Err(HttpCacheBuilderError::InvalidMaxSize {
                    max_size: MAX_CACHE_SIZE.to_string(),
                    min_size: MIN_CACHE_SIZE.to_string(),
                    size_bytes: max_size.as_u64(),
                });
            }
        }

        if let Some(ttl) = self.default_ttl {
            if !(MIN_TTL..=MAX_TTL).contains(&ttl) {
                return Err(HttpCacheBuilderError::InvalidTtl {
                    max_ttl: format!("{} seconds", MAX_TTL.as_secs()),
                    min_ttl: format!("{} seconds", MIN_TTL.as_secs()),
                    ttl: ttl.as_secs(),
                });
            }
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_test_builder(path: &str) -> HttpCacheBuilder {
        HttpCacheBuilder::new(Some(path))
    }

    #[test]
    fn test_cache_builder_with_defaults() {
        let mut builder = make_test_builder("test.db");
        assert_eq!(builder.db_path, Some(PathBuf::from("test.db")));
        assert_eq!(builder.max_size, None);
        assert_eq!(builder.default_ttl, None);
        assert!(builder.build().is_ok());
    }

    #[test]
    fn test_cache_builder_valid_custom() {
        let mut builder = make_test_builder("custom.db")
            .max_size(ByteSize::b(1024))
            .default_ttl(Duration::from_secs(60));

        assert_eq!(builder.db_path, Some(PathBuf::from("custom.db")));
        assert_eq!(builder.max_size, Some(ByteSize::b(1024)));
        assert_eq!(builder.default_ttl, Some(Duration::from_secs(60)));
        assert!(builder.build().is_ok());
    }

    #[test]
    fn test_validation_empty_db_path() {
        let result = make_test_builder("   ").build();
        assert!(matches!(result, Err(HttpCacheBuilderError::EmptyDbPath)));
    }

    #[test]
    fn test_validation_max_size_too_small() {
        let result = make_test_builder("test.db")
            .max_size(ByteSize::b(512))
            .build();
        assert!(matches!(
            result,
            Err(HttpCacheBuilderError::InvalidMaxSize {
                size_bytes: 512,
                min_size: _,
                max_size: _,
            })
        ));
    }

    #[test]
    fn test_validation_max_size_too_large() {
        let result = make_test_builder("test.db")
            .max_size(ByteSize::b(2 * 1024 * 1024 * 1024))
            .build();
        assert!(matches!(
            result,
            Err(HttpCacheBuilderError::InvalidMaxSize {
                size_bytes: 2147483648,
                min_size: _,
                max_size: _,
            })
        ));
    }

    #[test]
    fn test_validation_max_size_boundaries() {
        let mut builder_min = make_test_builder("test.db").max_size(MIN_CACHE_SIZE);
        assert!(builder_min.build().is_ok());

        let mut builder_max = make_test_builder("test.db").max_size(MAX_CACHE_SIZE);
        assert!(builder_max.build().is_ok());
    }

    #[test]
    fn test_validation_ttl_too_small() {
        let result = make_test_builder("test.db")
            .default_ttl(Duration::from_secs(0))
            .build();
        assert!(matches!(
            result,
            Err(HttpCacheBuilderError::InvalidTtl {
                ttl: 0,
                min_ttl: _,
                max_ttl: _,
            })
        ));
    }

    #[test]
    fn test_validation_ttl_too_large() {
        let result = make_test_builder("test.db")
            .default_ttl(Duration::from_secs(8 * 24 * 60 * 60))
            .build();
        assert!(matches!(
            result,
            Err(HttpCacheBuilderError::InvalidTtl {
                ttl: 691200,
                min_ttl: _,
                max_ttl: _,
            })
        ));
    }

    #[test]
    fn test_validation_ttl_boundaries() {
        let mut builder_min = make_test_builder("test.db").default_ttl(MIN_TTL);
        assert!(builder_min.build().is_ok());

        let mut builder_max = make_test_builder("test.db").default_ttl(MAX_TTL);
        assert!(builder_max.build().is_ok());
    }
}
