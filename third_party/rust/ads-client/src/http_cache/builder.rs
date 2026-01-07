/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::http_cache::HttpCache;

use super::bytesize::ByteSize;
use super::connection_initializer::HttpCacheConnectionInitializer;
use super::store::HttpCacheStore;
use rusqlite::Connection;
use sql_support::open_database;
use std::path::PathBuf;
use std::time::Duration;

const DEFAULT_MAX_SIZE: ByteSize = ByteSize::mib(10);
const DEFAULT_TTL: Duration = Duration::from_secs(300);

const MIN_CACHE_SIZE: ByteSize = ByteSize::kib(1);
const MAX_CACHE_SIZE: ByteSize = ByteSize::mib(100);
const MIN_TTL: Duration = Duration::from_secs(1);
const MAX_TTL: Duration = Duration::from_secs(60 * 60 * 24 * 7); // 7 days

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("Database path cannot be empty")]
    EmptyDbPath,
    #[error("Database error: {0}")]
    Database(#[from] open_database::Error),
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

#[derive(Debug)]
pub struct HttpCacheBuilder {
    db_path: PathBuf,
    max_size: Option<ByteSize>,
    default_ttl: Option<Duration>,
}

impl HttpCacheBuilder {
    pub fn new(db_path: impl Into<PathBuf>) -> Self {
        Self {
            db_path: db_path.into(),
            max_size: None,
            default_ttl: None,
        }
    }

    #[cfg(test)]
    pub fn new_for_tests(db_path: impl Into<PathBuf>) -> Self {
        Self {
            db_path: db_path.into(),
            max_size: None,
            default_ttl: None,
        }
    }

    pub fn max_size(mut self, max_size: ByteSize) -> Self {
        self.max_size = Some(max_size);
        self
    }

    pub fn default_ttl(mut self, ttl: Duration) -> Self {
        self.default_ttl = Some(ttl);
        self
    }

    fn validate(&self) -> Result<(), Error> {
        if self.db_path.to_string_lossy().trim().is_empty() {
            return Err(Error::EmptyDbPath);
        }

        if let Some(max_size) = self.max_size {
            if max_size < MIN_CACHE_SIZE || max_size > MAX_CACHE_SIZE {
                return Err(Error::InvalidMaxSize {
                    size_bytes: max_size.as_u64(),
                    min_size: MIN_CACHE_SIZE.to_string(),
                    max_size: MAX_CACHE_SIZE.to_string(),
                });
            }
        }

        if let Some(ttl) = self.default_ttl {
            if !(MIN_TTL..=MAX_TTL).contains(&ttl) {
                return Err(Error::InvalidTtl {
                    ttl: ttl.as_secs(),
                    min_ttl: format!("{} seconds", MIN_TTL.as_secs()),
                    max_ttl: format!("{} seconds", MAX_TTL.as_secs()),
                });
            }
        }

        Ok(())
    }

    fn open_connection(&self) -> Result<Connection, Error> {
        let initializer = HttpCacheConnectionInitializer {};
        let conn = if cfg!(test) {
            open_database::open_memory_database(&initializer)?
        } else {
            open_database::open_database(&self.db_path, &initializer)?
        };
        Ok(conn)
    }

    pub fn build(&self) -> Result<HttpCache, Error> {
        self.validate()?;

        let conn = self.open_connection()?;
        let max_size = self.max_size.unwrap_or(DEFAULT_MAX_SIZE);
        let store = HttpCacheStore::new(conn);
        let default_ttl = self.default_ttl.unwrap_or(DEFAULT_TTL);

        Ok(HttpCache {
            max_size,
            store,
            default_ttl,
        })
    }

    #[cfg(test)]
    pub fn build_for_time_dependent_tests(&self) -> Result<HttpCache, Error> {
        self.validate()?;

        let conn = self.open_connection()?;
        let max_size = self.max_size.unwrap_or(DEFAULT_MAX_SIZE);
        let store = HttpCacheStore::new_with_test_clock(conn);
        let default_ttl = self.default_ttl.unwrap_or(DEFAULT_TTL);

        Ok(HttpCache {
            max_size,
            store,
            default_ttl,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_cache_builder_with_defaults() {
        let builder = HttpCacheBuilder::new("test.db".to_string());
        assert_eq!(builder.db_path, PathBuf::from("test.db"));
        assert_eq!(builder.max_size, None);
        assert_eq!(builder.default_ttl, None);
        assert!(builder.build().is_ok());
    }

    #[test]
    fn test_cache_builder_valid_custom() {
        let builder = HttpCacheBuilder::new("custom.db".to_string())
            .max_size(ByteSize::b(1024))
            .default_ttl(Duration::from_secs(60));

        assert_eq!(builder.db_path, PathBuf::from("custom.db"));
        assert_eq!(builder.max_size, Some(ByteSize::b(1024)));
        assert_eq!(builder.default_ttl, Some(Duration::from_secs(60)));
        assert!(builder.build().is_ok());
    }

    #[test]
    fn test_validation_empty_db_path() {
        let builder = HttpCacheBuilder::new("   ".to_string());

        let result = builder.build();
        assert!(matches!(result, Err(Error::EmptyDbPath)));
    }

    #[test]
    fn test_validation_max_size_too_small() {
        let builder = HttpCacheBuilder::new("test.db".to_string()).max_size(ByteSize::b(512));

        let result = builder.build();
        assert!(matches!(
            result,
            Err(Error::InvalidMaxSize {
                size_bytes: 512,
                min_size: _,
                max_size: _,
            })
        ));
    }

    #[test]
    fn test_validation_max_size_too_large() {
        let builder = HttpCacheBuilder::new("test.db".to_string())
            .max_size(ByteSize::b(2 * 1024 * 1024 * 1024));

        let result = builder.build();
        assert!(matches!(
            result,
            Err(Error::InvalidMaxSize {
                size_bytes: 2147483648,
                min_size: _,
                max_size: _,
            })
        ));
    }

    #[test]
    fn test_validation_max_size_boundaries() {
        let builder_min = HttpCacheBuilder::new("test.db".to_string()).max_size(MIN_CACHE_SIZE);
        assert!(builder_min.build().is_ok());

        let builder_max = HttpCacheBuilder::new("test.db".to_string()).max_size(MAX_CACHE_SIZE);
        assert!(builder_max.build().is_ok());
    }

    #[test]
    fn test_validation_ttl_too_small() {
        let builder =
            HttpCacheBuilder::new("test.db".to_string()).default_ttl(Duration::from_secs(0));

        let result = builder.build();
        assert!(matches!(
            result,
            Err(Error::InvalidTtl {
                ttl: 0,
                min_ttl: _,
                max_ttl: _,
            })
        ));
    }

    #[test]
    fn test_validation_ttl_too_large() {
        let builder = HttpCacheBuilder::new("test.db".to_string())
            .default_ttl(Duration::from_secs(8 * 24 * 60 * 60));

        let result = builder.build();
        assert!(matches!(
            result,
            Err(Error::InvalidTtl {
                ttl: 691200,
                min_ttl: _,
                max_ttl: _,
            })
        ));
    }

    #[test]
    fn test_validation_ttl_boundaries() {
        let builder_min = HttpCacheBuilder::new("test.db".to_string()).default_ttl(MIN_TTL);
        assert!(builder_min.build().is_ok());

        let builder_max = HttpCacheBuilder::new("test.db".to_string()).default_ttl(MAX_TTL);
        assert!(builder_max.build().is_ok());
    }
}
