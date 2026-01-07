/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{collections::HashMap, sync::Arc, time::Duration};

use crate::http_cache::{
    clock::{CacheClock, Clock},
    request_hash::RequestHash,
    ByteSize,
};
use parking_lot::Mutex;
use rusqlite::{params, Connection, OptionalExtension, Result as SqliteResult};
use viaduct::{Header, Request, Response};

#[cfg(test)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FaultKind {
    None,
    Lookup,
    Store,
    Trim,
    Cleanup,
}

pub struct HttpCacheStore {
    conn: Mutex<Connection>,
    clock: Arc<dyn Clock>,
    #[cfg(test)]
    fault: parking_lot::Mutex<FaultKind>,
}

impl HttpCacheStore {
    pub fn new(conn: Connection) -> Self {
        Self {
            conn: Mutex::new(conn),
            clock: Arc::new(CacheClock),
            #[cfg(test)]
            fault: parking_lot::Mutex::new(FaultKind::None),
        }
    }

    #[cfg(test)]
    pub fn new_with_test_clock(conn: Connection) -> Self {
        use crate::http_cache::clock::TestClock;

        Self {
            conn: Mutex::new(conn),
            clock: Arc::new(TestClock::new(chrono::Utc::now().timestamp())),
            #[cfg(test)]
            fault: parking_lot::Mutex::new(FaultKind::None),
        }
    }

    #[cfg(test)]
    pub fn get_clock(&self) -> &dyn Clock {
        &*self.clock
    }

    /// Removes all entries from cache.
    pub fn clear_all(&self) -> SqliteResult<usize> {
        let conn = self.conn.lock();
        conn.execute("DELETE FROM http_cache", [])
    }

    /// Returns total size of the cache in bytes.
    pub fn current_total_size_bytes(&self) -> SqliteResult<ByteSize> {
        let conn = self.conn.lock();
        let size_bytes = conn.query_row(
            "SELECT COALESCE(SUM(size_bytes),0) FROM http_cache",
            [],
            |row| row.get(0),
        )?;
        Ok(ByteSize::b(size_bytes))
    }

    /// Removes all entries from the store who's expires_at is before the current time.
    pub fn delete_expired_entries(&self) -> SqliteResult<usize> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Cleanup {
            return Err(Self::forced_fault_error("forced cleanup failure"));
        }
        let conn = self.conn.lock();
        conn.execute(
            "DELETE FROM http_cache WHERE expires_at < ?1",
            params![self.clock.now_epoch_seconds()],
        )
    }

    /// Lookup is agnostic to expiration. If it exists in the store, it will return the result.
    pub fn lookup(&self, request: &Request) -> SqliteResult<Option<(Response, RequestHash)>> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Lookup {
            return Err(Self::forced_fault_error("forced lookup failure"));
        }
        let request_hash = RequestHash::from(request);
        let conn = self.conn.lock();
        conn.query_row(
            "SELECT response_body, response_headers, response_status FROM http_cache WHERE request_hash = ?1",
            params![request_hash.to_string()],
            |row| {
            let response_body = row.get(0)?;
            let response_headers: Vec<u8> = row.get(1)?;
            let response_status: i64 = row.get(2)?;
                let headers = serde_json::from_slice::<HashMap<String, String>>(&response_headers)
            .map(|map| {
                map.into_iter()
                    .filter_map(|(n, v)| Header::new(n, v).ok())
                    .collect::<Vec<_>>()
                    .into()
            })
            .unwrap_or_else(|_| viaduct::Headers::new());

        let response = Response {
            body: response_body,
            headers,
            request_method: request.method,
            status: response_status as u16,
            url: request.url.clone(),
        };
        Ok((response, request_hash))
            },
        )
        .optional()
    }

    /// Upsert an object into the store with an expires_at defined by the given ttl_seconds.
    /// Calling this method will always store an object regardless of headers or policy.
    /// Logic to determine the correct ttl or cache/no-cache should happen before calling this.
    pub fn store_with_ttl(
        &self,
        request: &Request,
        response: &Response,
        ttl: &Duration,
    ) -> SqliteResult<RequestHash> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Store {
            return Err(Self::forced_fault_error("forced store failure"));
        }
        let request_hash = RequestHash::from(request);
        let headers_map: HashMap<String, String> = response.headers.clone().into();
        let response_headers = serde_json::to_vec(&headers_map).unwrap_or_default();
        let size_bytes = (response_headers.len() + response.body.len()) as i64;
        let now = self.clock.now_epoch_seconds();
        let ttl_seconds = ttl.as_secs();
        let expires_at = now + ttl_seconds as i64;

        let conn = self.conn.lock();
        conn.execute(
            "INSERT INTO http_cache (
                cached_at,
                expires_at,
                request_hash,
                response_body,
                response_headers,
                response_status,
                size_bytes,
                ttl_seconds
            )
            VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
            ON CONFLICT(request_hash) DO UPDATE SET
                cached_at=excluded.cached_at,
                expires_at=excluded.expires_at,
                response_body=excluded.response_body,
                response_headers=excluded.response_headers,
                response_status=excluded.response_status,
                size_bytes=excluded.size_bytes,
                ttl_seconds=excluded.ttl_seconds",
            params![
                now,
                expires_at,
                request_hash.to_string(),
                response.body,
                response_headers,
                response.status,
                size_bytes,
                ttl_seconds as i64,
            ],
        )?;
        Ok(request_hash)
    }

    pub fn invalidate_by_hash(&self, request_hash: &RequestHash) -> SqliteResult<usize> {
        let conn = self.conn.lock();
        conn.execute(
            "DELETE FROM http_cache WHERE request_hash = ?1",
            params![request_hash.to_string()],
        )
    }

    /// Trim cache to
    pub fn trim_to_max_size(&self, max_size_bytes: i64) -> SqliteResult<()> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Trim {
            return Err(Self::forced_fault_error("forced trim failure"));
        }
        loop {
            let total = self.current_total_size_bytes()?;
            if total.as_u64() <= max_size_bytes as u64 {
                break;
            }
            let conn = self.conn.lock();
            conn.execute(
                "DELETE FROM http_cache WHERE rowid IN (
                    SELECT rowid FROM http_cache ORDER BY cached_at ASC LIMIT 1
                )",
                [],
            )?;
        }
        Ok(())
    }

    #[cfg(test)]
    pub fn set_fault(&self, kind: FaultKind) {
        *self.fault.lock() = kind;
    }

    #[cfg(test)]
    fn forced_fault_error(msg: &str) -> rusqlite::Error {
        rusqlite::Error::SqliteFailure(
            rusqlite::ffi::Error {
                code: rusqlite::ErrorCode::InternalMalfunction,
                extended_code: 0,
            },
            Some(msg.to_string()),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::http_cache::connection_initializer::HttpCacheConnectionInitializer;
    use sql_support::open_database;
    use std::time::Duration;
    use viaduct::{header_names, Headers, Method, Response};

    fn fetch_timestamps(store: &HttpCacheStore, req: &Request) -> (i64, i64, i64) {
        let hash = RequestHash::from(req).to_string();
        let conn = store.conn.lock();
        conn.query_row(
            "SELECT
                    cached_at,
                    expires_at,
                    COALESCE(ttl_seconds, -1)
            FROM http_cache WHERE request_hash = ?1",
            rusqlite::params![hash],
            |row| {
                let cached_at: i64 = row.get(0)?;
                let expires_at: i64 = row.get(1)?;
                let ttl: i64 = row.get(2)?;
                Ok((cached_at, expires_at, ttl))
            },
        )
        .expect("row should exist")
    }

    fn create_test_request(url: &str, body: &[u8]) -> Request {
        Request {
            method: Method::Get,
            url: url.parse().unwrap(),
            headers: Headers::new(),
            body: Some(body.to_vec()),
        }
    }

    fn create_test_response(status: u16, body: &[u8]) -> Response {
        let mut headers = Headers::new();
        headers
            .insert(header_names::CONTENT_TYPE, "application/json")
            .unwrap();

        Response {
            request_method: Method::Get,
            url: "https://example.com/test".parse().unwrap(),
            status,
            headers,
            body: body.to_vec(),
        }
    }

    fn create_test_store() -> HttpCacheStore {
        let initializer = HttpCacheConnectionInitializer {};
        let conn = open_database::open_memory_database(&initializer)
            .expect("failed to open memory cache db");
        HttpCacheStore::new_with_test_clock(conn)
    }

    #[test]
    fn test_lookup_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Lookup);

        let req = create_test_request("https://example.com/api", b"body");
        let err = store.lookup(&req).unwrap_err();

        match err {
            rusqlite::Error::SqliteFailure(_, Some(msg)) => {
                assert!(msg.contains("forced lookup failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_store_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Store);

        let req = create_test_request("https://example.com/api", b"body");
        let resp = create_test_response(200, b"resp");

        let err = store
            .store_with_ttl(&req, &resp, &Duration::new(300, 0))
            .unwrap_err();
        match err {
            rusqlite::Error::SqliteFailure(_, Some(msg)) => {
                assert!(msg.contains("forced store failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_trim_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Trim);

        let req = create_test_request("https://example.com/api", b"");
        let resp = create_test_response(200, b"resp");
        store
            .store_with_ttl(&req, &resp, &Duration::new(300, 0))
            .unwrap();

        let err = store.trim_to_max_size(1).unwrap_err();
        match err {
            rusqlite::Error::SqliteFailure(_, Some(msg)) => {
                assert!(msg.contains("forced trim failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_cleanup_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Cleanup);

        let err = store.delete_expired_entries().unwrap_err();
        match err {
            rusqlite::Error::SqliteFailure(_, Some(msg)) => {
                assert!(msg.contains("forced cleanup failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_store_with_ttl_sets_fields_consistently() {
        let store = create_test_store();
        let req = create_test_request("https://example.com/a", b"");
        let resp = create_test_response(200, b"X");

        let ttl = Duration::new(5, 0);
        store.store_with_ttl(&req, &resp, &ttl).unwrap();

        let (cached_at, expires_at, ttl_seconds) = fetch_timestamps(&store, &req);
        assert_eq!(ttl_seconds, ttl.as_secs() as i64);
        // expires_at should be cached_at + ttl (allow 1s skew)
        let diff = expires_at - cached_at;
        let ttl_seconds = ttl.as_secs();
        assert!(
            (diff == ttl_seconds as i64)
                || (diff == ttl_seconds as i64 - 1)
                || (diff == ttl_seconds as i64 + 1),
            "unexpected expires_at diff: got {diff}, want ~{ttl_seconds}"
        );
    }

    #[test]
    fn test_upsert_refreshes_ttl_and_expiry() {
        let store = create_test_store();
        let req = create_test_request("https://example.com/b", b"");
        let resp = create_test_response(200, b"Y");

        store
            .store_with_ttl(&req, &resp, &Duration::new(300, 0))
            .unwrap();
        let (c1, e1, t1) = fetch_timestamps(&store, &req);
        assert_eq!(t1, 300);

        store.get_clock().advance(3);

        store
            .store_with_ttl(&req, &resp, &Duration::new(1, 0))
            .unwrap();
        let (c2, e2, t2) = fetch_timestamps(&store, &req);
        assert_eq!(t2, 1);
        // cached_at should be >= previous cached_at; expires_at should move accordingly
        assert!(c2 > c1);
        assert!(e2 < e1, "expires_at should move earlier when TTL shrinks");
    }

    #[test]
    fn test_delete_expired_removes_only_expired() {
        let store = create_test_store();
        let req_exp = create_test_request("https://example.com/expired", b"");
        let req_fresh = create_test_request("https://example.com/fresh", b"");
        let resp = create_test_response(200, b"Z");

        // expired after ~1s, fresh after ~10s
        store
            .store_with_ttl(&req_exp, &resp, &Duration::new(1, 0))
            .unwrap();
        store
            .store_with_ttl(&req_fresh, &resp, &Duration::new(10, 0))
            .unwrap();

        // Both present now
        assert!(store.lookup(&req_exp).unwrap().is_some());
        assert!(store.lookup(&req_fresh).unwrap().is_some());

        // Let first one expire; then cleanup
        store.clock.advance(2);
        let removed = store.delete_expired_entries().unwrap();
        assert!(
            removed >= 1,
            "expected at least one expired row to be deleted"
        );

        // Expired is gone, fresh remains
        assert!(store.lookup(&req_exp).unwrap().is_none());
        assert!(store.lookup(&req_fresh).unwrap().is_some());
    }

    #[test]
    fn test_lookup_is_expired_agnostic() {
        let store = create_test_store();
        let req = create_test_request("https://example.com/stale", b"");
        let resp = create_test_response(200, b"W");

        store
            .store_with_ttl(&req, &resp, &Duration::new(1, 0))
            .unwrap();
        // Check that lookup still returns (store is policy-agnostic).
        store.clock.advance(2);
        assert!(store.lookup(&req).unwrap().is_some());

        // Test cleanup still removes it
        store.delete_expired_entries().unwrap();
        assert!(store.lookup(&req).unwrap().is_none());
    }

    #[test]
    fn test_zero_ttl_expires_immediately_after_tick() {
        // Because cleanup uses `expires_at < now`, a row with expires_at == now
        // won’t be removed until the clock advances at least one second.
        let store = create_test_store();
        let req = create_test_request("https://example.com/zero", b"");
        let resp = create_test_response(200, b"0");

        store
            .store_with_ttl(&req, &resp, &Duration::new(0, 0))
            .unwrap();
        assert!(store.lookup(&req).unwrap().is_some());

        // Advance a second so now > expires_at
        store.clock.advance(2);
        let removed = store.delete_expired_entries().unwrap();
        assert!(removed >= 1);
        assert!(store.lookup(&req).unwrap().is_none());
    }

    #[test]
    fn test_store_and_retrieve() {
        let store = create_test_store();

        let request = create_test_request("https://example.com/api", b"test body");
        let response = create_test_response(200, b"test response");

        store
            .store_with_ttl(&request, &response, &Duration::new(300, 0))
            .unwrap();

        let retrieved = store.lookup(&request).unwrap().unwrap();
        assert_eq!(retrieved.0.status, 200);
        assert_eq!(retrieved.0.body, b"test response");
    }

    #[test]
    fn test_ttl_expiration() {
        let store = create_test_store();

        let request = create_test_request("https://example.com/api", b"test body");
        let response = create_test_response(200, b"test response");

        store
            .store_with_ttl(&request, &response, &Duration::new(300, 0))
            .unwrap();

        let retrieved = store.lookup(&request).unwrap().unwrap();
        assert_eq!(retrieved.0.body, b"test response");

        store.clock.advance(2);

        let retrieved_after_expiry = store.lookup(&request).unwrap();
        assert!(retrieved_after_expiry.is_some());
    }

    #[test]
    fn test_max_size_eviction() {
        let initializer = HttpCacheConnectionInitializer {};
        let conn = open_database::open_memory_database(&initializer)
            .expect("failed to open memory cache db");
        let store = HttpCacheStore::new(conn);

        for i in 0..5 {
            let request = create_test_request(&format!("https://example.com/api/{}", i), b"");
            let large_body = vec![0u8; 300];
            let response = create_test_response(200, &large_body);
            store
                .store_with_ttl(&request, &response, &Duration::new(300, 0))
                .unwrap();
        }

        // Trim to max size of 1024 bytes
        store.trim_to_max_size(1024).unwrap();

        let total_size = store.current_total_size_bytes().unwrap();
        assert!(total_size.as_u64() <= 1024);

        let first_request = create_test_request("https://example.com/api/0", b"");
        let first_cached = store.lookup(&first_request).unwrap();
        assert!(first_cached.is_none());
    }

    #[test]
    fn test_no_store_directive() {
        let store = create_test_store();
        let request = create_test_request("https://example.com/api", b"test body");

        let mut response = create_test_response(200, b"test response");
        response
            .headers
            .insert("cache-control", "no-store")
            .unwrap();

        store
            .store_with_ttl(&request, &response, &Duration::new(300, 0))
            .unwrap();
        let retrieved = store.lookup(&request).unwrap();
        assert!(retrieved.is_some());
    }

    #[test]
    fn test_clear_all() {
        let store = create_test_store();

        let request1 = create_test_request("https://example.com/api1", b"test body 1");
        let response1 = create_test_response(200, b"test response 1");
        store
            .store_with_ttl(&request1, &response1, &Duration::new(300, 0))
            .unwrap();

        let request2 = create_test_request("https://example.com/api2", b"test body 2");
        let response2 = create_test_response(200, b"test response 2");
        store
            .store_with_ttl(&request2, &response2, &Duration::new(300, 0))
            .unwrap();

        // Verify both are cached
        assert!(store.lookup(&request1).unwrap().is_some());
        assert!(store.lookup(&request2).unwrap().is_some());

        // Clear all entries
        let deleted_count = store.clear_all().unwrap();
        assert_eq!(deleted_count, 2);

        // Verify both are cleared
        assert!(store.lookup(&request1).unwrap().is_none());
        assert!(store.lookup(&request2).unwrap().is_none());
    }

    #[test]
    fn test_invalidate_by_hash() {
        let store = create_test_store();

        let request1 = create_test_request("https://example.com/api1", b"body1");
        let request2 = create_test_request("https://example.com/api2", b"body2");
        let resp = create_test_response(200, b"resp");

        store
            .store_with_ttl(&request1, &resp, &Duration::new(300, 0))
            .unwrap();
        store
            .store_with_ttl(&request2, &resp, &Duration::new(300, 0))
            .unwrap();

        assert!(store.lookup(&request1).unwrap().is_some());
        assert!(store.lookup(&request2).unwrap().is_some());

        let hash1 = RequestHash::from(&request1);
        let deleted = store.invalidate_by_hash(&hash1).unwrap();
        assert_eq!(deleted, 1);

        assert!(store.lookup(&request1).unwrap().is_none());
        assert!(store.lookup(&request2).unwrap().is_some());
    }
}
