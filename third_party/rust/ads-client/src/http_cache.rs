/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod builder;
mod bytesize;
mod cache_control;
mod clock;
mod connection_initializer;
mod request_hash;
mod store;

use self::{builder::HttpCacheBuilder, cache_control::CacheControl, store::HttpCacheStore};

use viaduct::{Request, Response};

pub use self::bytesize::ByteSize;
pub use self::request_hash::RequestHash;
use std::cmp;
use std::path::Path;
use std::time::Duration;

pub type HttpCacheSendResult<T> = std::result::Result<T, viaduct::ViaductError>;

#[derive(Clone, Copy, Debug, Default)]
pub struct RequestCachePolicy {
    pub mode: CacheMode,
    pub ttl_seconds: Option<u64>, // optional client-defined ttl override
}

#[derive(Clone, Copy, Debug, Default)]
pub enum CacheMode {
    #[default]
    CacheFirst,
    NetworkFirst,
}

impl CacheMode {
    pub fn execute(
        &self,
        cache: &HttpCache,
        request: &Request,
        ttl: &Duration,
    ) -> HttpCacheSendResult<SendOutcome> {
        match self {
            CacheMode::CacheFirst => self.exec_cache_first(cache, request, ttl),
            CacheMode::NetworkFirst => self.exec_network_first(cache, request, ttl),
        }
    }

    fn exec_cache_first(
        &self,
        cache: &HttpCache,
        request: &Request,
        ttl: &Duration,
    ) -> HttpCacheSendResult<SendOutcome> {
        match cache.store.lookup(request) {
            Ok(Some((resp, _))) => Ok(SendOutcome {
                response: resp,
                cache_outcome: CacheOutcome::Hit,
            }),
            Ok(None) => cache.request_from_network_then_cache(request, ttl),
            Err(e) => {
                let response = request.clone().send()?;
                Ok(SendOutcome {
                    response,
                    cache_outcome: CacheOutcome::LookupFailed(e),
                })
            }
        }
    }

    fn exec_network_first(
        &self,
        cache: &HttpCache,
        request: &Request,
        ttl: &Duration,
    ) -> HttpCacheSendResult<SendOutcome> {
        cache.request_from_network_then_cache(request, ttl)
    }
}

#[derive(Debug, thiserror::Error)]
pub enum HttpCacheError {
    #[error("Could not build cache: {0}")]
    Builder(#[from] builder::Error),

    #[error("SQLite operation failed: {0}")]
    Sqlite(#[from] rusqlite::Error),
}

#[derive(Debug)]
pub enum CacheOutcome {
    Hit,
    LookupFailed(rusqlite::Error), // cache miss path due to lookup error
    NoCache,                       // send policy requested a cache bypass
    MissNotCacheable,              // policy says "don't store"
    MissStored,                    // stored successfully
    StoreFailed(HttpCacheError),   // insert/upsert failed
    CleanupFailed(HttpCacheError), // cleaning expired objects failed
}

pub struct SendOutcome {
    pub response: Response,
    pub cache_outcome: CacheOutcome,
}

pub struct HttpCache {
    max_size: ByteSize,
    store: HttpCacheStore,
    default_ttl: Duration,
}

impl HttpCache {
    pub fn builder<P: AsRef<Path>>(db_path: P) -> HttpCacheBuilder {
        HttpCacheBuilder::new(db_path.as_ref())
    }

    pub fn clear(&self) -> Result<(), HttpCacheError> {
        self.store.clear_all().map_err(HttpCacheError::from)?;
        Ok(())
    }

    pub fn invalidate_by_hash(&self, request_hash: &RequestHash) -> Result<(), HttpCacheError> {
        self.store
            .invalidate_by_hash(request_hash)
            .map_err(HttpCacheError::from)?;
        Ok(())
    }

    pub fn send_with_policy(
        &self,
        request: &Request,
        request_policy: &RequestCachePolicy,
    ) -> HttpCacheSendResult<SendOutcome> {
        let request_policy_ttl = match request_policy.ttl_seconds {
            Some(s) => Duration::new(s, 0),
            None => self.default_ttl,
        };

        request_policy
            .mode
            .execute(self, request, &request_policy_ttl)
    }

    fn request_from_network_then_cache(
        &self,
        request: &Request,
        request_policy_ttl: &Duration,
    ) -> HttpCacheSendResult<SendOutcome> {
        let response = request.clone().send()?;
        if let Err(e) = self.store.delete_expired_entries() {
            return Ok(SendOutcome {
                response,
                cache_outcome: CacheOutcome::CleanupFailed(e.into()),
            });
        }
        let cache_control = CacheControl::from(&response);
        let cache_outcome = if cache_control.should_cache() {
            let response_ttl = match cache_control.max_age {
                Some(s) => Duration::new(s, 0),
                None => self.default_ttl,
            };

            // We respect the smallest ttl between the policy, default client value, or header
            let final_ttl = cmp::min(
                cmp::min(*request_policy_ttl, self.default_ttl),
                response_ttl,
            );

            if final_ttl.as_secs() == 0 {
                return Ok(SendOutcome {
                    response,
                    cache_outcome: CacheOutcome::NoCache,
                });
            }

            match self.cache_object(request, &response, &final_ttl) {
                Ok(()) => CacheOutcome::MissStored,
                Err(e) => CacheOutcome::StoreFailed(e),
            }
        } else {
            CacheOutcome::MissNotCacheable
        };

        Ok(SendOutcome {
            response,
            cache_outcome,
        })
    }

    fn cache_object(
        &self,
        request: &Request,
        response: &Response,
        ttl: &Duration,
    ) -> Result<(), HttpCacheError> {
        self.store.store_with_ttl(request, response, ttl)?;
        self.store.trim_to_max_size(self.max_size.as_u64() as i64)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use mockito::mock;

    use super::*;

    fn make_post_request() -> Request {
        let url = format!("{}/ads", mockito::server_url()).parse().unwrap();
        Request::post(url).json(&serde_json::json!({"fake":"data"}))
    }

    fn make_cache() -> HttpCache {
        // Our store opens an in-memory cache for tests. So the name is irrelevant.
        HttpCache::builder("ignored_in_tests.db")
            .default_ttl(Duration::from_secs(60))
            .max_size(ByteSize::mib(1))
            .build()
            .expect("cache build should succeed")
    }

    fn make_cache_with_ttl(secs: u64) -> HttpCache {
        // In tests our store uses an in-memory DB; filename is irrelevant.
        HttpCache::builder("ignored_in_tests.db")
            .default_ttl(Duration::from_secs(secs))
            .max_size(ByteSize::mib(4))
            .build_for_time_dependent_tests()
            .expect("cache build should succeed")
    }

    #[test]
    fn test_http_cache_creation() {
        // Test that HttpCache can be created successfully with test config
        let cache = HttpCache::builder("test_cache.db").build();
        assert!(cache.is_ok());

        // Test with custom config
        let cache_with_config = HttpCache::builder("custom_test.db")
            .max_size(ByteSize::mib(1))
            .default_ttl(Duration::from_secs(60))
            .build();
        assert!(cache_with_config.is_ok());
    }

    #[test]
    fn test_clear_cache() {
        let cache = HttpCache::builder("test_clear.db").build().unwrap();

        // Create a test request and response
        let request = viaduct::Request {
            method: viaduct::Method::Get,
            url: "https://example.com/test".parse().unwrap(),
            headers: viaduct::Headers::new(),
            body: None,
        };

        let response = viaduct::Response {
            request_method: viaduct::Method::Get,
            url: "https://example.com/test".parse().unwrap(),
            status: 200,
            headers: viaduct::Headers::new(),
            body: b"test response".to_vec(),
        };

        // Store something in the cache
        cache
            .store
            .store_with_ttl(&request, &response, &Duration::new(300, 0))
            .unwrap();

        // Verify it's cached
        let retrieved = cache.store.lookup(&request).unwrap();
        assert!(retrieved.is_some());

        // Clear the cache
        cache.clear().unwrap();

        // Verify it's cleared
        let retrieved_after_clear = cache.store.lookup(&request).unwrap();
        assert!(retrieved_after_clear.is_none());
    }

    #[test]
    fn test_default_policy_miss_then_store_then_hit() {
        viaduct_dev::init_backend_dev();

        let body = r#"{"ok":true}"#;
        let _m = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(body)
            .expect(1) // only the first call should hit the network
            .create();

        let cache = make_cache();
        let req = make_post_request();

        // First call: miss -> store
        let o1 = cache
            .send_with_policy(&req.clone(), &RequestCachePolicy::default())
            .unwrap();
        matches!(o1.cache_outcome, CacheOutcome::MissStored);

        // Second call: hit (no extra HTTP request due to expect(1))
        let o2 = cache
            .send_with_policy(&req, &RequestCachePolicy::default())
            .unwrap();
        matches!(o2.cache_outcome, CacheOutcome::Hit);
        assert_eq!(o2.response.status, 200);
    }

    #[test]
    fn test_refresh_policy_always_uses_network_then_caches() {
        viaduct_dev::init_backend_dev();

        let body1 = r#"{"ok":true,"n":1}"#;
        let body2 = r#"{"ok":true,"n":2}"#;
        // Two live responses expected on refresh
        let _m1 = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(body1)
            .create();
        let _m2 = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(body2)
            .create();

        let cache = make_cache();
        let req = make_post_request();

        // First refresh: live -> MissStored
        let o1 = cache
            .send_with_policy(
                &req.clone(),
                &RequestCachePolicy {
                    mode: CacheMode::NetworkFirst,
                    ttl_seconds: None,
                },
            )
            .unwrap();
        matches!(o1.cache_outcome, CacheOutcome::MissStored);

        // Second refresh: live again (different body), still MissStored
        let o2 = cache
            .send_with_policy(
                &req,
                &RequestCachePolicy {
                    mode: CacheMode::NetworkFirst,
                    ttl_seconds: None,
                },
            )
            .unwrap();
        matches!(o2.cache_outcome, CacheOutcome::MissStored);
        assert_eq!(o2.response.status, 200);
    }

    #[test]
    fn test_not_cacheable_no_store() {
        viaduct_dev::init_backend_dev();

        let _m = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "no-store") // should block caching
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let cache = make_cache();
        let req = make_post_request();

        let o = cache
            .send_with_policy(&req.clone(), &RequestCachePolicy::default())
            .unwrap();
        matches!(o.cache_outcome, CacheOutcome::MissNotCacheable);

        // Next call should hit network again (since we didn't cache)
        let _m2 = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();
        let o2 = cache
            .send_with_policy(&req, &RequestCachePolicy::default())
            .unwrap();
        // Either MissStored (if headers differ) or MissNotCacheable if still no-store
        assert!(matches!(
            o2.cache_outcome,
            CacheOutcome::MissStored | CacheOutcome::MissNotCacheable
        ));
    }

    #[test]
    fn ttl_resolution_min_of_server_request_default() {
        viaduct_dev::init_backend_dev();

        let _m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "max-age=1") // Set max age to 1 second
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let cache = make_cache_with_ttl(300);
        let req = make_post_request();
        let policy = RequestCachePolicy {
            mode: CacheMode::CacheFirst,
            ttl_seconds: Some(20), // 20 second ttl specified vs the cache's default of 300s
        };

        // Store ttl should resolve to 1s as specified by response headers
        let out = cache.send_with_policy(&req, &policy).unwrap();
        assert!(matches!(out.cache_outcome, CacheOutcome::MissStored));

        // After ~>1s, cleanup should remove it
        cache.store.get_clock().advance(2);

        cache.store.delete_expired_entries().unwrap();

        assert!(cache.store.lookup(&req).unwrap().is_none());
    }

    #[test]
    fn ttl_resolution_request_overrides_default_when_smaller() {
        viaduct_dev::init_backend_dev();

        let _m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let cache = make_cache_with_ttl(60);
        let req = make_post_request();
        let policy = RequestCachePolicy {
            mode: CacheMode::CacheFirst,
            ttl_seconds: Some(2),
        };

        // Store with effective TTL = 2s
        let out = cache.send_with_policy(&req, &policy).unwrap();
        assert!(matches!(out.cache_outcome, CacheOutcome::MissStored));

        // Not expired yet at ~1s
        cache.store.get_clock().advance(1);
        cache.store.delete_expired_entries().unwrap();
        assert!(cache.store.lookup(&req).unwrap().is_some());

        // Expired after ~2s
        cache.store.get_clock().advance(2);
        cache.store.delete_expired_entries().unwrap();
        assert!(cache.store.lookup(&req).unwrap().is_none());
    }

    #[test]
    fn ttl_resolution_uses_default_when_no_server_and_no_request_override() {
        viaduct_dev::init_backend_dev();

        let _m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json") // No response policy ttl
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let cache = make_cache_with_ttl(2);
        let req = make_post_request();
        let policy = RequestCachePolicy::default(); // No request polity ttl

        // Store with effective TTL = 1s from client
        let out = cache.send_with_policy(&req, &policy).unwrap();
        assert!(matches!(out.cache_outcome, CacheOutcome::MissStored));

        // Not expired at ~1s
        cache.store.get_clock().advance(1);
        cache.store.delete_expired_entries().unwrap();
        assert!(cache.store.lookup(&req).unwrap().is_some());

        // Expired after ~3s
        cache.store.get_clock().advance(3);
        cache.store.delete_expired_entries().unwrap();
        assert!(cache.store.lookup(&req).unwrap().is_none());
    }

    #[test]
    fn test_invalidate_by_hash() {
        use crate::http_cache::request_hash::RequestHash;

        let cache = HttpCache::builder("test_invalidate.db").build().unwrap();

        let request1 = viaduct::Request {
            method: viaduct::Method::Post,
            url: "https://example.com/api1".parse().unwrap(),
            headers: viaduct::Headers::new(),
            body: Some(b"body1".to_vec()),
        };

        let request2 = viaduct::Request {
            method: viaduct::Method::Post,
            url: "https://example.com/api2".parse().unwrap(),
            headers: viaduct::Headers::new(),
            body: Some(b"body2".to_vec()),
        };

        let response = viaduct::Response {
            request_method: viaduct::Method::Post,
            url: "https://example.com/test".parse().unwrap(),
            status: 200,
            headers: viaduct::Headers::new(),
            body: b"test response".to_vec(),
        };

        cache
            .store
            .store_with_ttl(&request1, &response, &Duration::new(300, 0))
            .unwrap();

        cache
            .store
            .store_with_ttl(&request2, &response, &Duration::new(300, 0))
            .unwrap();

        assert!(cache.store.lookup(&request1).unwrap().is_some());
        assert!(cache.store.lookup(&request2).unwrap().is_some());

        let hash1 = RequestHash::from(&request1);
        cache.invalidate_by_hash(&hash1).unwrap();

        assert!(cache.store.lookup(&request1).unwrap().is_none());
        assert!(cache.store.lookup(&request2).unwrap().is_some());
    }
}
