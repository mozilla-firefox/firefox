/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{collections::HashMap, time::Duration};

use crate::http_cache::{
    connection_initializer::HttpCacheConnectionInitializer, request_hash::RequestHash, ByteSize,
};
use chrono::{DateTime, Utc};
use error_support::warn;
use rusqlite::{params, OptionalExtension};
use sql_support::{open_database, LazyDb};
use viaduct::{Header, Response};

#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaultKind {
    Cleanup,
    Lookup,
    None,
    Store,
    Trim,
}

pub(super) struct HttpCacheStore {
    db: LazyDb<HttpCacheConnectionInitializer>,
    #[cfg(test)]
    fault: parking_lot::Mutex<FaultKind>,
}

impl HttpCacheStore {
    pub(super) fn new(db: LazyDb<HttpCacheConnectionInitializer>) -> Self {
        Self {
            db,
            #[cfg(test)]
            fault: parking_lot::Mutex::new(FaultKind::None),
        }
    }

    /// Removes all entries from cache.
    pub(super) fn clear_all(&self) -> Result<usize, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        Ok(conn.execute("DELETE FROM http_cache", [])?)
    }

    pub(super) fn close(&self) {
        self.db.close(true);
    }

    /// Returns whether the db is in memory rather than backed by a file.
    pub fn is_memory(&self) -> Result<bool, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        // SQLite reports an empty path for in-memory dbs.
        Ok(conn.path() == Some(""))
    }

    /// Returns total size of the cache in bytes.
    #[cfg(test)]
    pub(super) fn current_total_size_bytes(&self) -> Result<ByteSize, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        let size_bytes = conn.query_row(
            "SELECT COALESCE(SUM(size_bytes),0) FROM http_cache",
            [],
            |row| row.get(0),
        )?;
        Ok(ByteSize::b(size_bytes))
    }

    /// Removes all entries from the store whose expires_at is at or before `now`.
    pub(super) fn delete_expired_entries(
        &self,
        now: DateTime<Utc>,
    ) -> Result<usize, open_database::Error> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Cleanup {
            return Err(Self::forced_fault_error("forced cleanup failure"))?;
        }
        let (conn, _) = self.db.lock()?;
        Ok(conn.execute(
            "DELETE FROM http_cache WHERE expires_at <= ?1",
            params![now.timestamp()],
        )?)
    }

    pub(super) fn invalidate_by_hash(
        &self,
        request_hash: &RequestHash,
    ) -> Result<usize, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        Ok(conn.execute(
            "DELETE FROM http_cache WHERE request_hash = ?1",
            params![request_hash.to_string()],
        )?)
    }

    /// Lookup is agnostic to expiration. If it exists in the store, it will return the result.
    pub(super) fn lookup(
        &self,
        request_hash: &RequestHash,
    ) -> Result<Option<Response>, open_database::Error> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Lookup {
            return Err(Self::forced_fault_error("forced lookup failure"))?;
        }
        let (conn, _) = self.db.lock()?;
        Ok(conn.query_row(
            "SELECT response_body, response_headers, response_status, request_method, request_url
             FROM http_cache
             WHERE request_hash = ?1",
            params![request_hash.to_string()],
            |row| {
                let response_body: Vec<u8> = row.get(0)?;
                let response_headers: Vec<u8> = row.get(1)?;
                let response_status: i64 = row.get(2)?;
                let method_str: String = row.get(3)?;
                let url_str: String = row.get(4)?;

                let headers = serde_json::from_slice::<HashMap<String, String>>(&response_headers)
                    .map_or_else(
                        |e| {
                            warn!("Ignoring unreadable cached headers: {e}");
                            viaduct::Headers::new()
                        },
                        |map| {
                            map.into_iter()
                                .filter_map(|(n, v)| {
                                    Header::new(n, v)
                                        .inspect_err(|e| warn!("Skipping a cached header: {e}"))
                                        .ok()
                                })
                                .collect::<Vec<_>>()
                                .into()
                        },
                    );

                let request_method = match method_str.as_str() {
                    "HEAD" => viaduct::Method::Head,
                    "POST" => viaduct::Method::Post,
                    "PUT" => viaduct::Method::Put,
                    "DELETE" => viaduct::Method::Delete,
                    "PATCH" => viaduct::Method::Patch,
                    // "GET", and anything this cache did not write.
                    _ => viaduct::Method::Get,
                };

                let url = url::Url::parse(&url_str).map_err(|e| {
                    rusqlite::Error::FromSqlConversionFailure(
                        4,
                        rusqlite::types::Type::Text,
                        format!("invalid URL in cache: {url_str}: {e}").into(),
                    )
                })?;
                let status = u16::try_from(response_status).map_err(|e| {
                    rusqlite::Error::FromSqlConversionFailure(
                        2,
                        rusqlite::types::Type::Integer,
                        Box::new(e),
                    )
                })?;

                Ok(Response {
                    body: response_body,
                    headers,
                    request_method,
                    status,
                    url,
                })
            },
        )
        .optional()?)
    }

    #[cfg(test)]
    pub fn set_fault(&self, kind: FaultKind) {
        *self.fault.lock() = kind;
    }

    /// Upsert an object into the store, cached at `now` and expiring `ttl` later.
    /// Calling this method will always store an object regardless of headers or policy.
    /// Logic to determine the correct ttl or cache/no-cache should happen before calling this.
    pub(super) fn store_with_ttl(
        &self,
        request_hash: &RequestHash,
        response: &Response,
        ttl: Duration,
        now: DateTime<Utc>,
    ) -> Result<(), open_database::Error> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Store {
            return Err(Self::forced_fault_error("forced store failure"))?;
        }
        let headers_map: HashMap<String, String> = response.headers.clone().into();
        let response_headers = serde_json::to_vec(&headers_map).unwrap_or_default();
        let size_bytes = response_headers.len() + response.body.len();
        let expires_at = now + ttl;

        let (conn, _) = self.db.lock()?;
        conn.execute(
            "INSERT INTO http_cache (
                cached_at,
                expires_at,
                request_hash,
                request_method,
                request_url,
                response_body,
                response_headers,
                response_status,
                size_bytes,
                ttl_seconds
            )
            VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
            ON CONFLICT(request_hash) DO UPDATE SET
                cached_at=excluded.cached_at,
                expires_at=excluded.expires_at,
                request_method=excluded.request_method,
                request_url=excluded.request_url,
                response_body=excluded.response_body,
                response_headers=excluded.response_headers,
                response_status=excluded.response_status,
                size_bytes=excluded.size_bytes,
                ttl_seconds=excluded.ttl_seconds",
            params![
                now.timestamp(),
                expires_at.timestamp(),
                request_hash.to_string(),
                response.request_method.as_str(),
                response.url.as_str(),
                response.body,
                response_headers,
                response.status,
                size_bytes,
                ttl.as_secs(),
            ],
        )?;
        Ok(())
    }

    /// Deletes the oldest entries until the cache fits in `max_size`.
    pub(super) fn trim_to_max_size(&self, max_size: ByteSize) -> Result<(), open_database::Error> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Trim {
            return Err(Self::forced_fault_error("forced trim failure"))?;
        }
        // Keeps the newest rows that fit in `max_size` and deletes the rest,
        // in one statement.
        let (conn, _) = self.db.lock()?;
        conn.execute(
            "DELETE FROM http_cache
             WHERE rowid IN (
                 SELECT rowid
                 FROM (
                     SELECT rowid,
                            SUM(size_bytes) OVER (ORDER BY cached_at DESC, rowid DESC) AS kept_bytes
                     FROM http_cache
                 )
                 WHERE kept_bytes > ?1
             )",
            params![max_size.as_u64()],
        )?;
        Ok(())
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
    use rusqlite::OpenFlags;
    use sql_support::open_database;
    use std::{path::Path, time::Duration};
    use viaduct::{header_names, Headers, Method, Request, Response};

    fn hash_for_request(req: &Request) -> RequestHash {
        RequestHash::new(&(req.method.as_str(), req.url.as_str()))
    }

    fn fixed_now() -> DateTime<Utc> {
        "2025-11-06T08:49:37Z".parse().unwrap()
    }

    /// The entry's `cached_at`, `expires_at` and TTL, read back from SQL.
    fn fetch_timestamps(
        store: &HttpCacheStore,
        hash: &RequestHash,
    ) -> (DateTime<Utc>, DateTime<Utc>, Duration) {
        let (conn, _) = store.db.lock().unwrap();
        let (cached_at, expires_at, ttl_seconds) = conn
            .query_row(
                "SELECT cached_at, expires_at, ttl_seconds
                 FROM http_cache
                 WHERE request_hash = ?1",
                rusqlite::params![hash.to_string()],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .expect("row should exist");
        (
            DateTime::from_timestamp(cached_at, 0).unwrap(),
            DateTime::from_timestamp(expires_at, 0).unwrap(),
            Duration::from_secs(ttl_seconds),
        )
    }

    fn create_test_request(url: &str, body: &[u8]) -> Request {
        Request {
            body: Some(body.to_vec()),
            headers: Headers::new(),
            method: Method::Get,
            url: url.parse().unwrap(),
        }
    }

    fn create_test_response(status: u16, body: &[u8]) -> Response {
        let mut headers = Headers::new();
        headers
            .insert(header_names::CONTENT_TYPE, "application/json")
            .unwrap();

        Response {
            body: body.to_vec(),
            headers,
            request_method: Method::Get,
            status,
            url: "https://example.com/test".parse().unwrap(),
        }
    }

    fn create_test_store() -> HttpCacheStore {
        let initializer = HttpCacheConnectionInitializer {};
        let db = LazyDb::new(
            Path::new(crate::IN_MEMORY_DB_PATH),
            OpenFlags::default(),
            initializer,
        );
        HttpCacheStore::new(db)
    }

    #[test]
    fn test_lookup_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Lookup);

        let req = create_test_request("https://example.com/api", b"body");
        let hash = hash_for_request(&req);
        let err = store.lookup(&hash).unwrap_err();

        match err {
            open_database::Error::SqlError(rusqlite::Error::SqliteFailure(_, Some(msg))) => {
                assert!(msg.contains("forced lookup failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_store_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Store);

        let resp = create_test_response(200, b"resp");
        let hash = hash_for_request(&create_test_request("https://example.com/api", b"body"));

        let err = store
            .store_with_ttl(&hash, &resp, Duration::from_secs(300), fixed_now())
            .unwrap_err();
        match err {
            open_database::Error::SqlError(rusqlite::Error::SqliteFailure(_, Some(msg))) => {
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
        let hash = hash_for_request(&req);
        let resp = create_test_response(200, b"resp");
        store
            .store_with_ttl(&hash, &resp, Duration::from_secs(300), fixed_now())
            .unwrap();

        let err = store.trim_to_max_size(ByteSize::b(1)).unwrap_err();
        match err {
            open_database::Error::SqlError(rusqlite::Error::SqliteFailure(_, Some(msg))) => {
                assert!(msg.contains("forced trim failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_cleanup_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Cleanup);

        let err = store.delete_expired_entries(fixed_now()).unwrap_err();
        match err {
            open_database::Error::SqlError(rusqlite::Error::SqliteFailure(_, Some(msg))) => {
                assert!(msg.contains("forced cleanup failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_store_with_ttl_sets_fields_consistently() {
        let store = create_test_store();
        let req = create_test_request("https://example.com/a", b"");
        let hash = hash_for_request(&req);
        let resp = create_test_response(200, b"X");

        let now = fixed_now();
        let ttl = Duration::from_secs(5);
        store.store_with_ttl(&hash, &resp, ttl, now).unwrap();

        assert_eq!(fetch_timestamps(&store, &hash), (now, now + ttl, ttl));
    }

    #[test]
    fn test_upsert_refreshes_ttl_and_expiry() {
        let store = create_test_store();
        let req = create_test_request("https://example.com/b", b"");
        let hash = hash_for_request(&req);
        let resp = create_test_response(200, b"Y");

        let now = fixed_now();
        store
            .store_with_ttl(&hash, &resp, Duration::from_secs(300), now)
            .unwrap();
        let (c1, e1, t1) = fetch_timestamps(&store, &hash);
        assert_eq!(t1, Duration::from_secs(300));

        store
            .store_with_ttl(
                &hash,
                &resp,
                Duration::from_secs(1),
                now + Duration::from_secs(3),
            )
            .unwrap();
        let (c2, e2, t2) = fetch_timestamps(&store, &hash);
        assert_eq!(t2, Duration::from_secs(1));
        assert!(c2 > c1);
        assert!(e2 < e1, "expires_at should move earlier when TTL shrinks");
    }

    #[test]
    fn test_delete_expired_removes_only_expired() {
        let store = create_test_store();
        let req_exp = create_test_request("https://example.com/expired", b"");
        let hash_exp = hash_for_request(&req_exp);
        let req_fresh = create_test_request("https://example.com/fresh", b"");
        let hash_fresh = hash_for_request(&req_fresh);
        let resp = create_test_response(200, b"Z");

        let now = fixed_now();
        store
            .store_with_ttl(&hash_exp, &resp, Duration::from_secs(1), now)
            .unwrap();
        store
            .store_with_ttl(&hash_fresh, &resp, Duration::from_secs(10), now)
            .unwrap();

        assert!(store.lookup(&hash_exp).unwrap().is_some());
        assert!(store.lookup(&hash_fresh).unwrap().is_some());

        let removed = store
            .delete_expired_entries(now + Duration::from_secs(2))
            .unwrap();
        assert!(
            removed >= 1,
            "expected at least one expired row to be deleted"
        );

        assert!(store.lookup(&hash_exp).unwrap().is_none());
        assert!(store.lookup(&hash_fresh).unwrap().is_some());
    }

    #[test]
    fn test_lookup_is_expired_agnostic() {
        let store = create_test_store();
        let req = create_test_request("https://example.com/stale", b"");
        let hash = hash_for_request(&req);
        let resp = create_test_response(200, b"W");

        let now = fixed_now();
        store
            .store_with_ttl(&hash, &resp, Duration::from_secs(1), now)
            .unwrap();
        assert!(store.lookup(&hash).unwrap().is_some());

        store
            .delete_expired_entries(now + Duration::from_secs(2))
            .unwrap();
        assert!(store.lookup(&hash).unwrap().is_none());
    }

    #[test]
    fn test_zero_ttl_expires_immediately_after_tick() {
        let store = create_test_store();
        let req = create_test_request("https://example.com/zero", b"");
        let hash = hash_for_request(&req);
        let resp = create_test_response(200, b"0");

        let now = fixed_now();
        store
            .store_with_ttl(&hash, &resp, Duration::from_secs(0), now)
            .unwrap();
        assert!(store.lookup(&hash).unwrap().is_some());

        let removed = store
            .delete_expired_entries(now + Duration::from_secs(2))
            .unwrap();
        assert!(removed >= 1);
        assert!(store.lookup(&hash).unwrap().is_none());
    }

    #[test]
    fn test_store_and_retrieve() {
        let store = create_test_store();

        let request = create_test_request("https://example.com/api", b"test body");
        let hash = hash_for_request(&request);
        let response = create_test_response(200, b"test response");

        store
            .store_with_ttl(&hash, &response, Duration::from_secs(300), fixed_now())
            .unwrap();

        let retrieved = store.lookup(&hash).unwrap().unwrap();
        assert_eq!(retrieved.status, 200);
        assert_eq!(retrieved.body, b"test response");
    }

    #[test]
    fn test_max_size_eviction() {
        let store = create_test_store();

        for i in 0..5 {
            let request = create_test_request(&format!("https://example.com/api/{}", i), b"");
            let hash = hash_for_request(&request);
            let large_body = vec![0u8; 300];
            let response = create_test_response(200, &large_body);
            store
                .store_with_ttl(&hash, &response, Duration::from_secs(300), fixed_now())
                .unwrap();
        }

        store.trim_to_max_size(ByteSize::kib(1)).unwrap();

        let total_size = store.current_total_size_bytes().unwrap();
        assert!(total_size.as_u64() <= 1024);

        let first_request = create_test_request("https://example.com/api/0", b"");
        let first_hash = hash_for_request(&first_request);
        let first_cached = store.lookup(&first_hash).unwrap();
        assert!(first_cached.is_none());

        let last_request = create_test_request("https://example.com/api/4", b"");
        let last_hash = hash_for_request(&last_request);
        assert!(store.lookup(&last_hash).unwrap().is_some());
    }

    #[test]
    fn test_no_store_directive() {
        let store = create_test_store();
        let request = create_test_request("https://example.com/api", b"test body");
        let hash = hash_for_request(&request);

        let mut response = create_test_response(200, b"test response");
        response
            .headers
            .insert("cache-control", "no-store")
            .unwrap();

        store
            .store_with_ttl(&hash, &response, Duration::from_secs(300), fixed_now())
            .unwrap();
        let retrieved = store.lookup(&hash).unwrap();
        assert!(retrieved.is_some());
    }

    #[test]
    fn test_clear_all() {
        let store = create_test_store();

        let request1 = create_test_request("https://example.com/api1", b"test body 1");
        let hash1 = hash_for_request(&request1);
        let response1 = create_test_response(200, b"test response 1");
        store
            .store_with_ttl(&hash1, &response1, Duration::from_secs(300), fixed_now())
            .unwrap();

        let request2 = create_test_request("https://example.com/api2", b"test body 2");
        let hash2 = hash_for_request(&request2);
        let response2 = create_test_response(200, b"test response 2");
        store
            .store_with_ttl(&hash2, &response2, Duration::from_secs(300), fixed_now())
            .unwrap();

        assert!(store.lookup(&hash1).unwrap().is_some());
        assert!(store.lookup(&hash2).unwrap().is_some());

        let deleted_count = store.clear_all().unwrap();
        assert_eq!(deleted_count, 2);

        assert!(store.lookup(&hash1).unwrap().is_none());
        assert!(store.lookup(&hash2).unwrap().is_none());
    }

    #[test]
    fn test_invalidate_by_hash() {
        let store = create_test_store();

        let request1 = create_test_request("https://example.com/api1", b"body1");
        let hash1 = hash_for_request(&request1);
        let request2 = create_test_request("https://example.com/api2", b"body2");
        let hash2 = hash_for_request(&request2);
        let resp = create_test_response(200, b"resp");

        store
            .store_with_ttl(&hash1, &resp, Duration::from_secs(300), fixed_now())
            .unwrap();
        store
            .store_with_ttl(&hash2, &resp, Duration::from_secs(300), fixed_now())
            .unwrap();

        assert!(store.lookup(&hash1).unwrap().is_some());
        assert!(store.lookup(&hash2).unwrap().is_some());

        let deleted = store.invalidate_by_hash(&hash1).unwrap();
        assert_eq!(deleted, 1);

        assert!(store.lookup(&hash1).unwrap().is_none());
        assert!(store.lookup(&hash2).unwrap().is_some());
    }
}
