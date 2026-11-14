/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod builder;
mod cache_control;
mod connection_initializer;
mod outcome;
mod request_hash;
mod response_age;
mod store;
mod strategy;
mod ttl;

use self::{
    builder::HttpCacheBuilder,
    store::HttpCacheStore,
    strategy::{CacheFirst, NetworkFirst},
};
use crate::bytesize::ByteSize;
use crate::clock::Clock;

use sql_support::open_database;
use std::hash::Hash;
use viaduct::{Client, Request, Response};

pub use self::builder::HttpCacheBuilderError;
pub use self::outcome::CacheOutcome;
pub use self::request_hash::RequestHash;
use std::path::Path;
use std::time::Duration;

pub type HttpCacheSendResult = Result<(Response, Vec<CacheOutcome>), viaduct::ViaductError>;

#[derive(Clone, Copy, Debug)]
pub enum CachePolicy {
    CacheFirst { ttl: Option<Duration> },
    NetworkFirst { ttl: Option<Duration> },
}

impl Default for CachePolicy {
    fn default() -> Self {
        Self::CacheFirst { ttl: None }
    }
}

pub struct HttpCache {
    clock: Clock,
    default_ttl: Duration,
    max_size: ByteSize,
    store: HttpCacheStore,
}

impl HttpCache {
    pub fn builder<P: AsRef<Path>>(db_path: P) -> HttpCacheBuilder {
        HttpCacheBuilder::new(Some(db_path.as_ref()))
    }

    pub fn builder_memory() -> HttpCacheBuilder {
        HttpCacheBuilder::new(None::<String>)
    }

    pub fn is_memory(&self) -> Result<bool, open_database::Error> {
        self.store.is_memory()
    }

    pub fn clear(&self) -> Result<(), open_database::Error> {
        self.store.clear_all()?;
        Ok(())
    }

    pub fn invalidate_by_hash(
        &self,
        request_hash: &RequestHash,
    ) -> Result<(), open_database::Error> {
        self.store.invalidate_by_hash(request_hash)?;
        Ok(())
    }

    pub fn send_with_policy<T: Hash + Into<Request>>(
        &self,
        client: &Client,
        item: T,
        policy: &CachePolicy,
    ) -> HttpCacheSendResult {
        let hash = RequestHash::new(&item);
        let request = item.into();
        let mut outcomes = vec![];

        // Clean up expired entries before applying the policy
        if let Err(e) = self.store.delete_expired_entries(self.clock.now()) {
            outcomes.push(CacheOutcome::CleanupFailed(e));
        }

        // Apply the cache policy and collect outcomes
        let (response, mut strategy_outcomes) = match policy {
            CachePolicy::CacheFirst { ttl } => CacheFirst {
                default_ttl: self.default_ttl,
                explicit_ttl: *ttl,
                hash,
                request,
            }
            .apply(client, &self.store, &self.clock),
            CachePolicy::NetworkFirst { ttl } => NetworkFirst {
                default_ttl: self.default_ttl,
                explicit_ttl: *ttl,
                hash,
                request,
            }
            .apply(client, &self.store, &self.clock),
        }?;
        outcomes.append(&mut strategy_outcomes);

        // Trim the cache to the max size only when something was actually stored
        if outcomes
            .iter()
            .any(|o| matches!(o, CacheOutcome::MissStored))
        {
            if let Err(e) = self.store.trim_to_max_size(self.max_size) {
                outcomes.push(CacheOutcome::TrimFailed(e));
            }
        }

        Ok((response, outcomes))
    }

    pub fn shutdown_db(&self) {
        self.store.close();
    }
}

#[cfg(test)]
mod tests {
    use mockito::mock;
    use std::hash::{Hash, Hasher};

    use super::*;
    use crate::clock::MockClock;
    use viaduct::ClientSettings;

    fn make_client() -> Client {
        Client::new(ClientSettings::default())
    }

    /// Test-only hashable wrapper around Request.
    /// Hashes method + url for cache key purposes.
    #[derive(Clone)]
    struct TestRequest(Request);

    impl Hash for TestRequest {
        fn hash<H: Hasher>(&self, state: &mut H) {
            self.0.method.as_str().hash(state);
            self.0.url.as_str().hash(state);
        }
    }

    impl From<TestRequest> for Request {
        fn from(t: TestRequest) -> Self {
            t.0
        }
    }

    fn make_post_request() -> TestRequest {
        let url = format!("{}/ads", mockito::server_url()).parse().unwrap();
        TestRequest(Request::post(url).json(&serde_json::json!({"fake":"data"})))
    }

    fn make_cache() -> HttpCache {
        // Our store opens an in-memory cache for tests. So the name is irrelevant.
        HttpCache::builder_memory()
            .default_ttl(Duration::from_secs(60))
            .max_size(ByteSize::mib(1))
            .build()
            .expect("cache build should succeed")
    }

    fn make_cache_with_ttl(secs: u64) -> (HttpCache, MockClock) {
        let clock = MockClock::new();
        // In tests our store uses an in-memory DB; filename is irrelevant.
        let cache = HttpCache::builder_memory()
            .default_ttl(Duration::from_secs(secs))
            .max_size(ByteSize::mib(4))
            .mock_clock(clock.clone())
            .build()
            .expect("cache build should succeed");
        (cache, clock)
    }

    /// Moves the cache's clock forward, then drops what expired by then.
    fn advance_and_expire(cache: &HttpCache, clock: &MockClock, by: Duration) {
        clock.advance(by);
        cache
            .store
            .delete_expired_entries(cache.clock.now())
            .unwrap();
    }

    #[test]
    fn test_http_cache_creation() {
        // Test that HttpCache can be created successfully with test config
        let cache: Result<HttpCache, _> = HttpCache::builder_memory().build();
        assert!(cache.is_ok());

        // Test with custom config
        let cache_with_config: Result<HttpCache, _> = HttpCache::builder_memory()
            .max_size(ByteSize::mib(1))
            .default_ttl(Duration::from_secs(60))
            .build();
        assert!(cache_with_config.is_ok());
    }

    #[test]
    fn test_clear_cache() {
        let cache: HttpCache = HttpCache::builder_memory().build().unwrap();

        // Create a test request and response
        let hash = RequestHash::new(&("Get", "https://example.com/test"));

        let response = viaduct::Response {
            body: b"test response".to_vec(),
            headers: viaduct::Headers::new(),
            request_method: viaduct::Method::Get,
            status: 200,
            url: "https://example.com/test".parse().unwrap(),
        };

        cache
            .store
            .store_with_ttl(
                &hash,
                &response,
                Duration::from_secs(300),
                cache.clock.now(),
            )
            .unwrap();

        // Verify it's cached
        let retrieved = cache.store.lookup(&hash).unwrap();
        assert!(retrieved.is_some());

        // Clear the cache
        cache.clear().unwrap();

        // Verify it's cleared
        let retrieved_after_clear = cache.store.lookup(&hash).unwrap();
        assert!(retrieved_after_clear.is_none());
    }

    #[test]
    fn test_default_policy_miss_then_store_then_hit() {
        viaduct_dev::init_backend_dev();

        let body = r#"{"ok":true}"#;
        let m = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(body)
            .expect(1) // only the first call should hit the network
            .create();

        let cache = make_cache();
        let req = make_post_request();
        let client = make_client();

        // First call: miss -> store
        let (_, outcomes) = cache
            .send_with_policy(&client, req.clone(), &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Second call: hit (no extra HTTP request due to expect(1))
        let (response, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::Hit));
        assert_eq!(response.status, 200);
        m.assert();
    }

    #[test]
    fn test_refresh_policy_always_uses_network_then_caches() {
        viaduct_dev::init_backend_dev();

        let body = r#"{"ok":true}"#;
        let m = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(body)
            .expect(2)
            .create();

        let cache = make_cache();
        let req = make_post_request();
        let client = make_client();

        // First refresh: live -> MissStored
        let (_, outcomes) = cache
            .send_with_policy(
                &client,
                req.clone(),
                &CachePolicy::NetworkFirst { ttl: None },
            )
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Second refresh: live again, still MissStored
        let (response, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::NetworkFirst { ttl: None })
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));
        assert_eq!(response.status, 200);
        m.assert();
    }

    #[test]
    fn test_not_cacheable_no_store() {
        viaduct_dev::init_backend_dev();

        let m = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "no-store") // should block caching
            .with_body(r#"{"ok":true}"#)
            .expect(2) // both calls should hit the network since the response isn't cacheable
            .create();

        let cache = make_cache();
        let req = make_post_request();
        let client = make_client();

        let (_, outcomes) = cache
            .send_with_policy(&client, req.clone(), &CachePolicy::default())
            .unwrap();
        assert!(matches!(
            outcomes.last().unwrap(),
            CacheOutcome::MissNotCacheable
        ));

        // Next call should hit network again (since we didn't cache)
        let (_, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::default())
            .unwrap();
        assert!(matches!(
            outcomes.last().unwrap(),
            CacheOutcome::MissNotCacheable
        ));
        m.assert();
    }

    #[test]
    fn test_error_response_is_not_cached() {
        viaduct_dev::init_backend_dev();

        let m = mock("POST", "/ads")
            .with_status(500)
            .with_header("content-type", "application/json")
            .with_body(r#"{"error":"unavailable"}"#)
            .expect(2) // both calls should hit the network: an error is not cached
            .create();

        let cache = make_cache();
        let req = make_post_request();
        let client = make_client();

        for _ in 0..2 {
            let (response, outcomes) = cache
                .send_with_policy(&client, req.clone(), &CachePolicy::default())
                .unwrap();
            assert_eq!(response.status, 500);
            assert!(matches!(
                outcomes.last().unwrap(),
                CacheOutcome::MissNotCacheable
            ));
        }
        m.assert();
    }

    #[test]
    fn ttl_resolution_explicit_overrides_server_max_age() {
        viaduct_dev::init_backend_dev();

        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "max-age=1") // Server says 1 second
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let (cache, clock) = make_cache_with_ttl(300);
        let req = make_post_request();
        let hash = RequestHash::new(&req);
        let policy = CachePolicy::CacheFirst {
            ttl: Some(Duration::from_secs(20)), // Caller asked for 20s
        };

        let client = make_client();
        // Caller's explicit TTL wins over the server's max-age.
        let (_, outcomes) = cache.send_with_policy(&client, req, &policy).unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Past the server max-age (1s) but still under the explicit TTL (20s) — entry is still there.
        advance_and_expire(&cache, &clock, Duration::from_secs(2));
        assert!(cache.store.lookup(&hash).unwrap().is_some());

        // Past the explicit TTL — entry should be expired.
        advance_and_expire(&cache, &clock, Duration::from_secs(20));
        assert!(cache.store.lookup(&hash).unwrap().is_none());
        m.assert();
    }

    #[test]
    fn ttl_resolution_uses_server_max_age_when_no_explicit_override() {
        viaduct_dev::init_backend_dev();

        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "max-age=2") // Server says 2 seconds
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        // Configured default is 300s — should be ignored in favor of server max-age.
        let (cache, clock) = make_cache_with_ttl(300);
        let req = make_post_request();
        let hash = RequestHash::new(&req);

        let client = make_client();
        let (_, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Still cached at ~1s.
        advance_and_expire(&cache, &clock, Duration::from_secs(1));
        assert!(cache.store.lookup(&hash).unwrap().is_some());

        // Expired after exceeding server max-age.
        advance_and_expire(&cache, &clock, Duration::from_secs(2));
        assert!(cache.store.lookup(&hash).unwrap().is_none());
        m.assert();
    }

    fn make_get_request(path: &str) -> TestRequest {
        let url = format!("{}{path}", mockito::server_url()).parse().unwrap();
        TestRequest(Request::get(url))
    }

    #[test]
    fn ttl_resolution_subtracts_age_header_from_server_max_age() {
        viaduct_dev::init_backend_dev();

        // What MARS sends, as a lower cache would hand it over 20 hours later.
        let m = mockito::mock("GET", "/v1/ads-preflight")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header(
                "cache-control",
                "max-age=86400, public, immutable, stale-while-revalidate=3600, stale-if-error=3600",
            )
            .with_header("age", "72000")
            .with_body(r#"{"geo_location":"US-CA","normalized_ua":""}"#)
            .expect(1)
            .create();

        let (cache, clock) = make_cache_with_ttl(300);
        let req = make_get_request("/v1/ads-preflight");
        let hash = RequestHash::new(&req);

        let client = make_client();
        let (_, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Stored for the 14400s of freshness left, not for another 86400s.
        advance_and_expire(&cache, &clock, Duration::from_secs(14_399));
        assert!(cache.store.lookup(&hash).unwrap().is_some());

        advance_and_expire(&cache, &clock, Duration::from_secs(1));
        assert!(cache.store.lookup(&hash).unwrap().is_none());
        m.assert();
    }

    #[test]
    fn ttl_resolution_subtracts_time_since_date_from_server_max_age() {
        viaduct_dev::init_backend_dev();

        let (cache, clock) = make_cache_with_ttl(300);
        let date = (clock.now() - Duration::from_secs(3000))
            .format("%a, %d %b %Y %H:%M:%S GMT")
            .to_string();
        let m = mockito::mock("GET", "/dated")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "max-age=3600")
            .with_header("date", &date)
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let req = make_get_request("/dated");
        let hash = RequestHash::new(&req);

        let client = make_client();
        let (_, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        advance_and_expire(&cache, &clock, Duration::from_secs(599));
        assert!(cache.store.lookup(&hash).unwrap().is_some());

        advance_and_expire(&cache, &clock, Duration::from_secs(1));
        assert!(cache.store.lookup(&hash).unwrap().is_none());
        m.assert();
    }

    #[test]
    fn response_older_than_server_max_age_is_not_stored() {
        viaduct_dev::init_backend_dev();

        let m = mockito::mock("GET", "/stale")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "max-age=3600")
            .with_header("age", "3600")
            .with_body(r#"{"ok":true}"#)
            .expect(2) // both calls go to the network: nothing was stored
            .create();

        let (cache, _) = make_cache_with_ttl(300);
        let req = make_get_request("/stale");
        let hash = RequestHash::new(&req);
        let client = make_client();

        for _ in 0..2 {
            let (response, outcomes) = cache
                .send_with_policy(&client, req.clone(), &CachePolicy::default())
                .unwrap();
            assert_eq!(response.status, 200);
            assert!(matches!(outcomes.last().unwrap(), CacheOutcome::NoCache));
            assert!(cache.store.lookup(&hash).unwrap().is_none());
        }
        m.assert();
    }

    #[test]
    fn ttl_resolution_explicit_ignores_response_age() {
        viaduct_dev::init_backend_dev();

        let m = mockito::mock("GET", "/explicit-stale")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_header("cache-control", "max-age=60")
            .with_header("age", "120")
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let (cache, clock) = make_cache_with_ttl(300);
        let req = make_get_request("/explicit-stale");
        let hash = RequestHash::new(&req);
        let policy = CachePolicy::CacheFirst {
            ttl: Some(Duration::from_secs(20)),
        };

        let client = make_client();
        let (_, outcomes) = cache.send_with_policy(&client, req, &policy).unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        advance_and_expire(&cache, &clock, Duration::from_secs(19));
        assert!(cache.store.lookup(&hash).unwrap().is_some());
        m.assert();
    }

    #[test]
    fn ttl_resolution_explicit_overrides_default() {
        viaduct_dev::init_backend_dev();

        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let (cache, clock) = make_cache_with_ttl(60);
        let req = make_post_request();
        let hash = RequestHash::new(&req);
        let policy = CachePolicy::CacheFirst {
            ttl: Some(Duration::from_secs(2)),
        };

        let client = make_client();
        // Store with effective TTL = 2s
        let (_, outcomes) = cache.send_with_policy(&client, req, &policy).unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Not expired yet at ~1s
        advance_and_expire(&cache, &clock, Duration::from_secs(1));
        assert!(cache.store.lookup(&hash).unwrap().is_some());

        // Expired after ~2s
        advance_and_expire(&cache, &clock, Duration::from_secs(2));
        assert!(cache.store.lookup(&hash).unwrap().is_none());
        m.assert();
    }

    #[test]
    fn ttl_resolution_uses_default_when_no_server_and_no_request_override() {
        viaduct_dev::init_backend_dev();

        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            // No response policy ttl
            .with_header("content-type", "application/json")
            .with_body(r#"{"ok":true}"#)
            .expect(1)
            .create();

        let (cache, clock) = make_cache_with_ttl(2);
        let req = make_post_request();
        let hash = RequestHash::new(&req);
        let client = make_client();
        // Store with effective TTL = 2s from client default
        let (_, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Not expired at ~1s
        advance_and_expire(&cache, &clock, Duration::from_secs(1));
        assert!(cache.store.lookup(&hash).unwrap().is_some());

        // Expired after ~3s
        advance_and_expire(&cache, &clock, Duration::from_secs(3));
        assert!(cache.store.lookup(&hash).unwrap().is_none());
        m.assert();
    }

    #[test]
    fn test_expired_entry_is_a_miss_on_next_send() {
        viaduct_dev::init_backend_dev();

        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(r#"{"ok":true}"#)
            .expect(2) // both calls should hit the network — second after the entry expires
            .create();

        let (cache, clock) = make_cache_with_ttl(2);
        let req = make_post_request();
        let client = make_client();

        // First call: miss -> store with 2s TTL
        let (_, outcomes) = cache
            .send_with_policy(&client, req.clone(), &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

        // Advance clock past the TTL
        clock.advance(Duration::from_secs(3));

        // Second call: expired entry must be a miss, not a hit
        let (_, outcomes) = cache
            .send_with_policy(&client, req, &CachePolicy::default())
            .unwrap();
        assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));
        m.assert();
    }

    #[test]
    fn test_invalidate_by_hash() {
        let cache: HttpCache = HttpCache::builder_memory().build().unwrap();

        let hash1 = RequestHash::new(&("Post", "https://example.com/api1"));
        let hash2 = RequestHash::new(&("Post", "https://example.com/api2"));

        let response = viaduct::Response {
            body: b"test response".to_vec(),
            headers: viaduct::Headers::new(),
            request_method: viaduct::Method::Post,
            status: 200,
            url: "https://example.com/test".parse().unwrap(),
        };

        cache
            .store
            .store_with_ttl(
                &hash1,
                &response,
                Duration::from_secs(300),
                cache.clock.now(),
            )
            .unwrap();

        cache
            .store
            .store_with_ttl(
                &hash2,
                &response,
                Duration::from_secs(300),
                cache.clock.now(),
            )
            .unwrap();

        assert!(cache.store.lookup(&hash1).unwrap().is_some());
        assert!(cache.store.lookup(&hash2).unwrap().is_some());

        cache.invalidate_by_hash(&hash1).unwrap();

        assert!(cache.store.lookup(&hash1).unwrap().is_none());
        assert!(cache.store.lookup(&hash2).unwrap().is_some());
    }
}
