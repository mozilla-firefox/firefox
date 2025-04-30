/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::request::{
    BatchPoster, InfoCollections, InfoConfiguration, PostQueue, PostResponse, PostResponseHandler,
};
use super::token;
use crate::bso::{IncomingBso, IncomingEncryptedBso, OutgoingBso, OutgoingEncryptedBso};
use crate::engine::{CollectionPost, CollectionRequest};
use crate::error::{self, Error, ErrorResponse};
use crate::record_types::MetaGlobalRecord;
use crate::{CollectionName, Guid, ServerTimestamp};
use serde_json::Value;
use std::str::FromStr;
use std::sync::atomic::{AtomicU32, Ordering};
use url::Url;
use viaduct::{
    header_names::{self, AUTHORIZATION},
    Method, Request, Response,
};

/// A response from a GET request on a Sync15StorageClient, encapsulating all
/// the variants users of this client needs to care about.
#[derive(Debug, Clone)]
pub enum Sync15ClientResponse<T> {
    Success {
        status: u16,
        record: T,
        last_modified: ServerTimestamp,
        route: String,
    },
    Error(ErrorResponse),
}

fn parse_seconds(seconds_str: &str) -> Option<u32> {
    let secs = seconds_str.parse::<f64>().ok()?.ceil();
    // Note: u32 doesn't impl TryFrom<f64> :(
    if !secs.is_finite() || secs < 0.0 || secs > f64::from(u32::MAX) {
        log::warn!("invalid backoff value: {}", secs);
        None
    } else {
        Some(secs as u32)
    }
}

impl<T> Sync15ClientResponse<T> {
    pub fn from_response(resp: Response, backoff_listener: &BackoffListener) -> error::Result<Self>
    where
        for<'a> T: serde::de::Deserialize<'a>,
    {
        let route: String = resp.url.path().into();
        // Android seems to respect retry_after even on success requests, so we
        // will too if it's present. This also lets us handle both backoff-like
        // properties in the same place.
        let retry_after = resp
            .headers
            .get(header_names::RETRY_AFTER)
            .and_then(parse_seconds);

        let backoff = resp
            .headers
            .get(header_names::X_WEAVE_BACKOFF)
            .and_then(parse_seconds);

        if let Some(b) = backoff {
            backoff_listener.note_backoff(b);
        }
        if let Some(ra) = retry_after {
            backoff_listener.note_retry_after(ra);
        }

        Ok(if resp.is_success() {
            let record: T = resp.json()?;
            let last_modified = resp
                .headers
                .get(header_names::X_LAST_MODIFIED)
                .and_then(|s| ServerTimestamp::from_str(s).ok())
                .ok_or(Error::MissingServerTimestamp)?;
            log::info!(
                "Successful request to \"{}\", incoming x-last-modified={:?}",
                route,
                last_modified
            );

            Sync15ClientResponse::Success {
                status: resp.status,
                record,
                last_modified,
                route,
            }
        } else {
            let status = resp.status;
            log::info!("Request \"{}\" was an error (status={})", route, status);
            match status {
                404 => Sync15ClientResponse::Error(ErrorResponse::NotFound { route }),
                401 => Sync15ClientResponse::Error(ErrorResponse::Unauthorized { route }),
                412 => Sync15ClientResponse::Error(ErrorResponse::PreconditionFailed { route }),
                500..=600 => {
                    Sync15ClientResponse::Error(ErrorResponse::ServerError { route, status })
                }
                _ => Sync15ClientResponse::Error(ErrorResponse::RequestFailed { route, status }),
            }
        })
    }

    pub fn create_storage_error(self) -> Error {
        let inner = match self {
            Sync15ClientResponse::Success { status, route, .. } => {
                // This should never happen as callers are expected to have
                // already special-cased this response, so warn if it does.
                // (or maybe we could panic?)
                log::warn!("Converting success response into an error");
                ErrorResponse::RequestFailed { status, route }
            }
            Sync15ClientResponse::Error(e) => e,
        };
        Error::StorageHttpError(inner)
    }
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Sync15StorageClientInit {
    pub key_id: String,
    pub access_token: String,
    pub tokenserver_url: Url,
}

/// A trait containing the methods required to run through the setup state
/// machine. This is factored out into a separate trait to make mocking
/// easier.
pub trait SetupStorageClient {
    fn fetch_info_configuration(&self) -> error::Result<Sync15ClientResponse<InfoConfiguration>>;
    fn fetch_info_collections(&self) -> error::Result<Sync15ClientResponse<InfoCollections>>;
    fn fetch_meta_global(&self) -> error::Result<Sync15ClientResponse<MetaGlobalRecord>>;
    fn fetch_crypto_keys(&self) -> error::Result<Sync15ClientResponse<IncomingEncryptedBso>>;

    fn put_meta_global(
        &self,
        xius: ServerTimestamp,
        global: &MetaGlobalRecord,
    ) -> error::Result<ServerTimestamp>;
    fn put_crypto_keys(
        &self,
        xius: ServerTimestamp,
        keys: &OutgoingEncryptedBso,
    ) -> error::Result<()>;
    fn wipe_all_remote(&self) -> error::Result<()>;
}

#[derive(Debug, Default)]
pub struct BackoffState {
    pub backoff_secs: AtomicU32,
    pub retry_after_secs: AtomicU32,
}

pub(crate) type BackoffListener = std::sync::Arc<BackoffState>;

pub(crate) fn new_backoff_listener() -> BackoffListener {
    std::sync::Arc::new(BackoffState::default())
}

impl BackoffState {
    pub fn note_backoff(&self, noted: u32) {
        super::util::atomic_update_max(&self.backoff_secs, noted)
    }

    pub fn note_retry_after(&self, noted: u32) {
        super::util::atomic_update_max(&self.retry_after_secs, noted)
    }

    pub fn get_backoff_secs(&self) -> u32 {
        self.backoff_secs.load(Ordering::SeqCst)
    }

    pub fn get_retry_after_secs(&self) -> u32 {
        self.retry_after_secs.load(Ordering::SeqCst)
    }

    pub fn get_required_wait(&self, ignore_soft_backoff: bool) -> Option<std::time::Duration> {
        let bo = self.get_backoff_secs();
        let ra = self.get_retry_after_secs();
        let secs = u64::from(if ignore_soft_backoff { ra } else { bo.max(ra) });
        if secs > 0 {
            Some(std::time::Duration::from_secs(secs))
        } else {
            None
        }
    }

    pub fn reset(&self) {
        self.backoff_secs.store(0, Ordering::SeqCst);
        self.retry_after_secs.store(0, Ordering::SeqCst);
    }
}

// meta/global is a clear-text Bso (ie, there's a String `payload` which has a MetaGlobalRecord)
// We don't use the 'content' helpers here because we want json errors to be fatal here
// (ie, we don't need tombstones and can't just skip a malformed record)
type IncMetaGlobalBso = IncomingBso;
type OutMetaGlobalBso = OutgoingBso;

#[derive(Debug)]
pub struct Sync15StorageClient {
    tsc: token::TokenProvider,
    pub(crate) backoff: BackoffListener,
}

impl SetupStorageClient for Sync15StorageClient {
    fn fetch_info_configuration(&self) -> error::Result<Sync15ClientResponse<InfoConfiguration>> {
        self.relative_storage_request(Method::Get, "info/configuration")
    }

    fn fetch_info_collections(&self) -> error::Result<Sync15ClientResponse<InfoCollections>> {
        self.relative_storage_request(Method::Get, "info/collections")
    }

    fn fetch_meta_global(&self) -> error::Result<Sync15ClientResponse<MetaGlobalRecord>> {
        let got: Sync15ClientResponse<IncMetaGlobalBso> =
            self.relative_storage_request(Method::Get, "storage/meta/global")?;
        Ok(match got {
            Sync15ClientResponse::Success {
                record,
                last_modified,
                route,
                status,
            } => {
                log::debug!(
                    "Got meta global with modified = {}; last-modified = {}",
                    record.envelope.modified,
                    last_modified
                );
                Sync15ClientResponse::Success {
                    record: serde_json::from_str(&record.payload)?,
                    last_modified,
                    route,
                    status,
                }
            }
            Sync15ClientResponse::Error(e) => Sync15ClientResponse::Error(e),
        })
    }

    fn fetch_crypto_keys(&self) -> error::Result<Sync15ClientResponse<IncomingEncryptedBso>> {
        self.relative_storage_request(Method::Get, "storage/crypto/keys")
    }

    fn put_meta_global(
        &self,
        xius: ServerTimestamp,
        global: &MetaGlobalRecord,
    ) -> error::Result<ServerTimestamp> {
        let bso = OutMetaGlobalBso::new(Guid::new("global").into(), global)?;
        self.put("storage/meta/global", xius, &bso)
    }

    fn put_crypto_keys(
        &self,
        xius: ServerTimestamp,
        keys: &OutgoingEncryptedBso,
    ) -> error::Result<()> {
        self.put("storage/crypto/keys", xius, keys)?;
        Ok(())
    }

    fn wipe_all_remote(&self) -> error::Result<()> {
        let s = self.tsc.api_endpoint()?;
        let url = Url::parse(&s)?;

        let req = self.build_request(Method::Delete, url)?;
        match self.exec_request::<Value>(req, false) {
            Ok(Sync15ClientResponse::Error(ErrorResponse::NotFound { .. }))
            | Ok(Sync15ClientResponse::Success { .. }) => Ok(()),
            Ok(resp) => Err(resp.create_storage_error()),
            Err(e) => Err(e),
        }
    }
}

impl Sync15StorageClient {
    pub fn new(init_params: Sync15StorageClientInit) -> error::Result<Sync15StorageClient> {
        rc_crypto::ensure_initialized();
        let tsc = token::TokenProvider::new(
            init_params.tokenserver_url,
            init_params.access_token,
            init_params.key_id,
        );
        Ok(Sync15StorageClient {
            tsc,
            backoff: new_backoff_listener(),
        })
    }

    pub fn get_encrypted_records(
        &self,
        collection_request: CollectionRequest,
    ) -> error::Result<Sync15ClientResponse<Vec<IncomingEncryptedBso>>> {
        self.collection_request(Method::Get, collection_request)
    }

    #[inline]
    fn authorized(&self, req: Request) -> error::Result<Request> {
        let hawk_header_value = self.tsc.authorization(&req)?;
        Ok(req.header(AUTHORIZATION, hawk_header_value)?)
    }

    // TODO: probably want a builder-like API to do collection requests (e.g. something
    // that occupies roughly the same conceptual role as the Collection class in desktop)
    fn build_request(&self, method: Method, url: Url) -> error::Result<Request> {
        self.authorized(Request::new(method, url).header(header_names::ACCEPT, "application/json")?)
    }

    fn relative_storage_request<P, T>(
        &self,
        method: Method,
        relative_path: P,
    ) -> error::Result<Sync15ClientResponse<T>>
    where
        P: AsRef<str>,
        for<'a> T: serde::de::Deserialize<'a>,
    {
        let s = self.tsc.api_endpoint()? + "/";
        let url = Url::parse(&s)?.join(relative_path.as_ref())?;
        self.exec_request(self.build_request(method, url)?, false)
    }

    fn exec_request<T>(
        &self,
        req: Request,
        require_success: bool,
    ) -> error::Result<Sync15ClientResponse<T>>
    where
        for<'a> T: serde::de::Deserialize<'a>,
    {
        log::trace!(
            "request: {} {} ({:?})",
            req.method,
            req.url.path(),
            req.url.query()
        );
        let resp = req.send()?;

        let result = Sync15ClientResponse::from_response(resp, &self.backoff)?;
        match result {
            Sync15ClientResponse::Success { .. } => Ok(result),
            _ => {
                if require_success {
                    Err(result.create_storage_error())
                } else {
                    Ok(result)
                }
            }
        }
    }

    fn collection_request<T>(
        &self,
        method: Method,
        r: CollectionRequest,
    ) -> error::Result<Sync15ClientResponse<T>>
    where
        for<'a> T: serde::de::Deserialize<'a>,
    {
        let url = build_collection_request_url(Url::parse(&self.tsc.api_endpoint()?)?, r)?;
        self.exec_request(self.build_request(method, url)?, false)
    }

    pub fn new_post_queue<'a, F: PostResponseHandler>(
        &'a self,
        coll: &'a CollectionName,
        config: &InfoConfiguration,
        ts: ServerTimestamp,
        on_response: F,
    ) -> error::Result<PostQueue<PostWrapper<'a>, F>> {
        let pw = PostWrapper { client: self, coll };
        Ok(PostQueue::new(config, ts, pw, on_response))
    }

    fn put<P, B>(
        &self,
        relative_path: P,
        xius: ServerTimestamp,
        body: &B,
    ) -> error::Result<ServerTimestamp>
    where
        P: AsRef<str>,
        B: serde::ser::Serialize,
    {
        let s = self.tsc.api_endpoint()? + "/";
        let url = Url::parse(&s)?.join(relative_path.as_ref())?;

        let req = self
            .build_request(Method::Put, url)?
            .json(body)
            .header(header_names::X_IF_UNMODIFIED_SINCE, format!("{}", xius))?;

        let resp = self.exec_request::<Value>(req, true)?;
        // Note: we pass `true` for require_success, so this panic never happens.
        if let Sync15ClientResponse::Success { last_modified, .. } = resp {
            Ok(last_modified)
        } else {
            unreachable!("Error returned exec_request when `require_success` was true");
        }
    }

    pub fn hashed_uid(&self) -> error::Result<String> {
        self.tsc.hashed_uid()
    }

    pub(crate) fn wipe_remote_engine(&self, engine: &str) -> error::Result<()> {
        let s = self.tsc.api_endpoint()? + "/";
        let url = Url::parse(&s)?.join(&format!("storage/{}", engine))?;
        log::debug!("Wiping: {:?}", url);
        let req = self.build_request(Method::Delete, url)?;
        match self.exec_request::<Value>(req, false) {
            Ok(Sync15ClientResponse::Error(ErrorResponse::NotFound { .. }))
            | Ok(Sync15ClientResponse::Success { .. }) => Ok(()),
            Ok(resp) => Err(resp.create_storage_error()),
            Err(e) => Err(e),
        }
    }
}

pub struct PostWrapper<'a> {
    client: &'a Sync15StorageClient,
    coll: &'a CollectionName,
}

impl BatchPoster for PostWrapper<'_> {
    fn post<T, O>(
        &self,
        bytes: Vec<u8>,
        xius: ServerTimestamp,
        batch: Option<String>,
        commit: bool,
        _: &PostQueue<T, O>,
    ) -> error::Result<PostResponse> {
        let r = CollectionPost::new(self.coll.clone())
            .batch(batch)
            .commit(commit);
        let url = build_collection_post_url(Url::parse(&self.client.tsc.api_endpoint()?)?, r)?;

        let req = self
            .client
            .build_request(Method::Post, url)?
            .header(header_names::CONTENT_TYPE, "application/json")?
            .header(header_names::X_IF_UNMODIFIED_SINCE, format!("{}", xius))?
            .body(bytes);
        self.client.exec_request(req, false)
    }
}

fn build_collection_url(mut base_url: Url, collection: CollectionName) -> error::Result<Url> {
    base_url
        .path_segments_mut()
        .map_err(|_| Error::UnacceptableUrl("Storage server URL is not a base".to_string()))?
        .extend(&["storage", &collection]);

    // This is strange but just accessing query_pairs_mut makes you have
    // a trailing question mark on your url. I don't think anything bad
    // would happen here, but I don't know, and also, it looks dumb so
    // I'd rather not have it.
    if base_url.query() == Some("") {
        base_url.set_query(None);
    }
    Ok(base_url)
}

fn build_collection_request_url(mut base_url: Url, r: CollectionRequest) -> error::Result<Url> {
    let mut pairs = base_url.query_pairs_mut();
    if r.full {
        pairs.append_pair("full", "1");
    }
    if let Some(ids) = &r.ids {
        // Most ids are 12 characters, and we comma separate them, so 13.
        let mut buf = String::with_capacity(ids.len() * 13);
        for (i, id) in ids.iter().enumerate() {
            if i > 0 {
                buf.push(',');
            }
            buf.push_str(id.as_str());
        }
        pairs.append_pair("ids", &buf);
    }
    if let Some(ts) = r.older {
        pairs.append_pair("older", &ts.to_string());
    }
    if let Some(ts) = r.newer {
        pairs.append_pair("newer", &ts.to_string());
    }
    if let Some(l) = r.limit {
        pairs.append_pair("sort", l.order.as_str());
        pairs.append_pair("limit", &l.num.to_string());
    }
    pairs.finish();
    drop(pairs);
    build_collection_url(base_url, r.collection)
}

#[cfg(feature = "sync-client")]
fn build_collection_post_url(mut base_url: Url, r: CollectionPost) -> error::Result<Url> {
    let mut pairs = base_url.query_pairs_mut();
    if let Some(batch) = &r.batch {
        pairs.append_pair("batch", batch);
    }
    if r.commit {
        pairs.append_pair("commit", "true");
    }
    pairs.finish();
    drop(pairs);
    build_collection_url(base_url, r.collection)
}

#[cfg(test)]
mod test {
    use super::*;
    #[test]
    fn test_send() {
        fn ensure_send<T: Send>() {}
        // Compile will fail if not send.
        ensure_send::<Sync15StorageClient>();
    }

    #[test]
    fn test_parse_seconds() {
        assert_eq!(parse_seconds("1"), Some(1));
        assert_eq!(parse_seconds("1.4"), Some(2));
        assert_eq!(parse_seconds("1.5"), Some(2));
        assert_eq!(parse_seconds("3600.0"), Some(3600));
        assert_eq!(parse_seconds("3600"), Some(3600));
        assert_eq!(parse_seconds("-1"), None);
        assert_eq!(parse_seconds("inf"), None);
        assert_eq!(parse_seconds("-inf"), None);
        assert_eq!(parse_seconds("one-thousand"), None);
        assert_eq!(parse_seconds("4294967295"), Some(4294967295));
        assert_eq!(parse_seconds("4294967296"), None);
    }

    #[test]
    fn test_query_building() {
        use crate::engine::RequestOrder;
        let base = Url::parse("https://example.com/sync").unwrap();

        let empty =
            build_collection_request_url(base.clone(), CollectionRequest::new("foo".into()))
                .unwrap();
        assert_eq!(empty.as_str(), "https://example.com/sync/storage/foo");

        let idreq = build_collection_request_url(
            base.clone(),
            CollectionRequest::new("wutang".into())
                .full()
                .ids(&["rza", "gza"]),
        )
        .unwrap();
        assert_eq!(
            idreq.as_str(),
            "https://example.com/sync/storage/wutang?full=1&ids=rza%2Cgza"
        );

        let complex = build_collection_request_url(
            base,
            CollectionRequest::new("specific".into())
                .full()
                .limit(10, RequestOrder::Oldest)
                .older_than(ServerTimestamp(9_876_540))
                .newer_than(ServerTimestamp(1_234_560)),
        )
        .unwrap();
        assert_eq!(complex.as_str(),
            "https://example.com/sync/storage/specific?full=1&older=9876.54&newer=1234.56&sort=oldest&limit=10");
    }

    #[cfg(feature = "sync-client")]
    #[test]
    fn test_post_query_building() {
        let base = Url::parse("https://example.com/sync").unwrap();

        let empty =
            build_collection_post_url(base.clone(), CollectionPost::new("foo".into())).unwrap();
        assert_eq!(empty.as_str(), "https://example.com/sync/storage/foo");
        let batch_start = build_collection_post_url(
            base.clone(),
            CollectionPost::new("bar".into())
                .batch(Some("true".into()))
                .commit(false),
        )
        .unwrap();
        assert_eq!(
            batch_start.as_str(),
            "https://example.com/sync/storage/bar?batch=true"
        );
        let batch_commit = build_collection_post_url(
            base,
            CollectionPost::new("asdf".into())
                .batch(Some("1234abc".into()))
                .commit(true),
        )
        .unwrap();
        assert_eq!(
            batch_commit.as_str(),
            "https://example.com/sync/storage/asdf?batch=1234abc&commit=true"
        );
    }
}
