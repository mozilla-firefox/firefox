/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::ops::RangeInclusive;
use std::time::Duration;

use super::AdsClient;
#[cfg(feature = "stateful")]
use crate::ads_store::{AdsStore, AdsStoreError};
use crate::bytesize::ByteSize;
use crate::http_cache::{HttpCache, HttpCacheError};
use crate::mars::{Environment, MARSClient};
use crate::non_empty_string::NonEmptyString;
use crate::telemetry;
use crate::IN_MEMORY_DB_PATH;

const DEFAULT_CACHE_DB_PATH: &str = IN_MEMORY_DB_PATH;
const DEFAULT_CACHE_MAX_SIZE: ByteSize = ByteSize::mib(10);
const DEFAULT_CACHE_TTL: Duration = Duration::from_secs(300);
#[cfg(feature = "stateful")]
const DEFAULT_STORE_DB_PATH: &str = IN_MEMORY_DB_PATH;
#[cfg(feature = "stateful")]
const DEFAULT_STORE_MAX_SIZE: ByteSize = ByteSize::mib(10);

const CACHE_MAX_SIZES: RangeInclusive<ByteSize> = ByteSize::kib(1)..=ByteSize::mib(100);
const CACHE_TTLS: RangeInclusive<Duration> =
    Duration::from_secs(1)..=Duration::from_secs(7 * 24 * 60 * 60);

#[derive(Default)]
pub(crate) struct AdsClientBuilder {
    cache_db_path: Option<String>,
    cache_default_ttl: Option<Duration>,
    cache_max_size: Option<ByteSize>,
    environment: Option<Environment>,
    #[cfg(feature = "stateful")]
    store_db_path: Option<String>,
    user_agent: Option<NonEmptyString>,
}

impl AdsClientBuilder {
    /// # Telemetry
    ///
    /// Records each invalid setting, and each database that fails to open, in
    /// `ads_client.build_cache_error`.
    pub(crate) fn build(&self) -> AdsClient {
        let (db_path, default_ttl, max_size) = self.http_cache_settings();
        let http_cache = HttpCache::new(db_path, default_ttl, max_size)
            .inspect_err(telemetry::record_build_cache_error)
            .ok();

        #[cfg(feature = "stateful")]
        let ads_store = AdsStore::new(self.ads_store_db_path(), DEFAULT_STORE_MAX_SIZE)
            .inspect_err(telemetry::record_ads_store_error)
            .ok();

        let client = MARSClient::new(
            self.environment.clone().unwrap_or_default(),
            http_cache,
            self.user_agent.clone(),
        );
        AdsClient::new(
            client,
            #[cfg(feature = "stateful")]
            ads_store,
        )
    }

    pub(crate) fn cache_db_path(&mut self, db_path: String) -> &mut Self {
        self.cache_db_path = Some(db_path);
        self
    }

    pub(crate) fn cache_default_ttl(&mut self, default_ttl: Option<Duration>) -> &mut Self {
        self.cache_default_ttl = default_ttl;
        self
    }

    pub(crate) fn cache_max_size(&mut self, max_size: Option<ByteSize>) -> &mut Self {
        self.cache_max_size = max_size;
        self
    }

    pub(crate) fn environment(&mut self, environment: Environment) -> &mut Self {
        self.environment = Some(environment);
        self
    }

    #[cfg(feature = "stateful")]
    pub(crate) fn store_db_path(&mut self, db_path: String) -> &mut Self {
        self.store_db_path = Some(db_path);
        self
    }

    pub(crate) fn user_agent(&mut self, user_agent: String) -> &mut Self {
        self.user_agent = NonEmptyString::new(user_agent);
        self
    }

    /// # Telemetry
    ///
    /// Records a blank path in `ads_client.build_cache_error`.
    #[cfg(feature = "stateful")]
    fn ads_store_db_path(&self) -> String {
        match &self.store_db_path {
            Some(db_path) if !db_path.trim().is_empty() => db_path.clone(),
            Some(_) => {
                telemetry::record_ads_store_error(&AdsStoreError::EmptyDbPath);
                DEFAULT_STORE_DB_PATH.to_string()
            }
            None => DEFAULT_STORE_DB_PATH.to_string(),
        }
    }

    /// # Telemetry
    ///
    /// Records each invalid setting in `ads_client.build_cache_error`.
    fn http_cache_settings(&self) -> (String, Duration, ByteSize) {
        let db_path = match &self.cache_db_path {
            Some(db_path) if !db_path.trim().is_empty() => db_path.clone(),
            Some(_) => {
                telemetry::record_build_cache_error(&HttpCacheError::EmptyDbPath);
                DEFAULT_CACHE_DB_PATH.to_string()
            }
            None => DEFAULT_CACHE_DB_PATH.to_string(),
        };
        let default_ttl = match self.cache_default_ttl {
            Some(ttl) if CACHE_TTLS.contains(&ttl) => ttl,
            Some(ttl) => {
                telemetry::record_build_cache_error(&HttpCacheError::InvalidTtl {
                    max_ttl: format!("{} seconds", CACHE_TTLS.end().as_secs()),
                    min_ttl: format!("{} seconds", CACHE_TTLS.start().as_secs()),
                    ttl: ttl.as_secs(),
                });
                DEFAULT_CACHE_TTL
            }
            None => DEFAULT_CACHE_TTL,
        };
        let max_size = match self.cache_max_size {
            Some(max_size) if CACHE_MAX_SIZES.contains(&max_size) => max_size,
            Some(max_size) => {
                telemetry::record_build_cache_error(&HttpCacheError::InvalidMaxSize {
                    max_size: CACHE_MAX_SIZES.end().to_string(),
                    min_size: CACHE_MAX_SIZES.start().to_string(),
                    size_bytes: max_size.as_u64(),
                });
                DEFAULT_CACHE_MAX_SIZE
            }
            None => DEFAULT_CACHE_MAX_SIZE,
        };
        (db_path, default_ttl, max_size)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn http_cache_settings(
        default_ttl: Duration,
        max_size: ByteSize,
    ) -> (String, Duration, ByteSize) {
        AdsClientBuilder::default()
            .cache_db_path("ads_cache.sqlite".to_string())
            .cache_default_ttl(Some(default_ttl))
            .cache_max_size(Some(max_size))
            .http_cache_settings()
    }

    #[test]
    fn test_cache_defaults() {
        let (db_path, default_ttl, max_size) = AdsClientBuilder::default().http_cache_settings();
        assert_eq!(db_path, IN_MEMORY_DB_PATH);
        assert_eq!(default_ttl, Duration::from_secs(300));
        assert_eq!(max_size, ByteSize::mib(10));
    }

    #[test]
    fn test_cache_settings_are_kept() {
        let (db_path, default_ttl, max_size) =
            http_cache_settings(Duration::from_secs(1800), ByteSize::mib(20));
        assert_eq!(db_path, "ads_cache.sqlite");
        assert_eq!(default_ttl, Duration::from_secs(1800));
        assert_eq!(max_size, ByteSize::mib(20));
    }

    #[test]
    fn test_cache_settings_bounds_are_valid() {
        for ttl in [*CACHE_TTLS.start(), *CACHE_TTLS.end()] {
            assert_eq!(http_cache_settings(ttl, ByteSize::mib(1)).1, ttl);
        }
        for max_size in [*CACHE_MAX_SIZES.start(), *CACHE_MAX_SIZES.end()] {
            assert_eq!(
                http_cache_settings(Duration::from_secs(60), max_size).2,
                max_size
            );
        }
    }

    #[test]
    fn test_invalid_cache_setting_falls_back_to_its_default_only() {
        // An invalid setting is recorded, so don't race the telemetry tests.
        let _lock = telemetry::test_lock();
        for ttl in [Duration::ZERO, Duration::from_secs(8 * 24 * 60 * 60)] {
            let (db_path, default_ttl, max_size) = http_cache_settings(ttl, ByteSize::mib(20));
            assert_eq!(db_path, "ads_cache.sqlite");
            assert_eq!(default_ttl, DEFAULT_CACHE_TTL);
            assert_eq!(max_size, ByteSize::mib(20));
        }
        for size in [ByteSize::b(512), ByteSize::mib(101)] {
            let (_, default_ttl, max_size) = http_cache_settings(Duration::from_secs(60), size);
            assert_eq!(default_ttl, Duration::from_secs(60));
            assert_eq!(max_size, DEFAULT_CACHE_MAX_SIZE);
        }

        let (db_path, _, _) = AdsClientBuilder::default()
            .cache_db_path("  ".to_string())
            .http_cache_settings();
        assert_eq!(db_path, IN_MEMORY_DB_PATH);
    }

    #[cfg(feature = "stateful")]
    #[test]
    fn test_blank_store_path_falls_back_to_memory() {
        // A blank path is recorded, so don't race the telemetry tests.
        let _lock = telemetry::test_lock();
        let db_path = AdsClientBuilder::default()
            .store_db_path(" ".to_string())
            .ads_store_db_path();
        assert_eq!(db_path, IN_MEMORY_DB_PATH);
    }
}
