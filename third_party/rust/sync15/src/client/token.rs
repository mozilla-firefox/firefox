/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::{self, Error as ErrorKind, Result};
use crate::ServerTimestamp;
use rc_crypto::hawk;
use serde_derive::*;
use std::borrow::{Borrow, Cow};
use std::cell::RefCell;
use std::fmt;
use std::time::{Duration, SystemTime};
use url::Url;
use viaduct::{header_names, Request};

const RETRY_AFTER_DEFAULT_MS: u64 = 10000;

// The TokenserverToken is the token as received directly from the token server
// and deserialized from JSON.
#[derive(Deserialize, Clone, PartialEq, Eq)]
struct TokenserverToken {
    id: String,
    key: String,
    api_endpoint: String,
    uid: u64,
    duration: u64,
    hashed_fxa_uid: String,
}

impl std::fmt::Debug for TokenserverToken {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("TokenserverToken")
            .field("api_endpoint", &self.api_endpoint)
            .field("uid", &self.uid)
            .field("duration", &self.duration)
            .field("hashed_fxa_uid", &self.hashed_fxa_uid)
            .finish()
    }
}

// The struct returned by the TokenFetcher - the token itself and the
// server timestamp.
struct TokenFetchResult {
    token: TokenserverToken,
    server_timestamp: ServerTimestamp,
}

// The trait for fetching tokens - we'll provide a "real" implementation but
// tests will re-implement it.
trait TokenFetcher {
    fn fetch_token(&self) -> crate::Result<TokenFetchResult>;
    // We allow the trait to tell us what the time is so tests can get funky.
    fn now(&self) -> SystemTime;
}

// Our "real" token fetcher, implementing the TokenFetcher trait, which hits
// the token server
#[derive(Debug)]
struct TokenServerFetcher {
    // The stuff needed to fetch a token.
    server_url: Url,
    access_token: String,
    key_id: String,
}

fn fixup_server_url(mut url: Url) -> url::Url {
    // The given `url` is the end-point as returned by .well-known/fxa-client-configuration,
    // or as directly specified by self-hosters. As a result, it may or may not have
    // the sync 1.5 suffix of "/1.0/sync/1.5", so add it on here if it does not.
    if url.as_str().ends_with("1.0/sync/1.5") {
        // ok!
    } else if url.as_str().ends_with("1.0/sync/1.5/") {
        // Shouldn't ever be Err() here, but the result is `Result<PathSegmentsMut, ()>`
        // and I don't want to unwrap or add a new error type just for PathSegmentsMut failing.
        if let Ok(mut path) = url.path_segments_mut() {
            path.pop();
        }
    } else {
        // We deliberately don't use `.join()` here in order to preserve all path components.
        // For example, "http://example.com/token" should produce "http://example.com/token/1.0/sync/1.5"
        // but using `.join()` would produce "http://example.com/1.0/sync/1.5".
        if let Ok(mut path) = url.path_segments_mut() {
            path.pop_if_empty();
            path.extend(&["1.0", "sync", "1.5"]);
        }
    };
    url
}

impl TokenServerFetcher {
    fn new(base_url: Url, access_token: String, key_id: String) -> TokenServerFetcher {
        TokenServerFetcher {
            server_url: fixup_server_url(base_url),
            access_token,
            key_id,
        }
    }
}

impl TokenFetcher for TokenServerFetcher {
    fn fetch_token(&self) -> Result<TokenFetchResult> {
        log::debug!("Fetching token from {}", self.server_url);
        let resp = Request::get(self.server_url.clone())
            .header(
                header_names::AUTHORIZATION,
                format!("Bearer {}", self.access_token),
            )?
            .header(header_names::X_KEYID, self.key_id.clone())?
            .send()?;

        if !resp.is_success() {
            log::warn!("Non-success status when fetching token: {}", resp.status);
            // TODO: the body should be JSON and contain a status parameter we might need?
            log::trace!("  Response body {}", resp.text());
            // XXX - shouldn't we "chain" these errors - ie, a BackoffError could
            // have a TokenserverHttpError as its cause?
            if let Some(res) = resp.headers.get_as::<f64, _>(header_names::RETRY_AFTER) {
                let ms = res
                    .ok()
                    .map_or(RETRY_AFTER_DEFAULT_MS, |f| (f * 1000f64) as u64);
                let when = self.now() + Duration::from_millis(ms);
                return Err(ErrorKind::BackoffError(when));
            }
            let status = resp.status;
            return Err(ErrorKind::TokenserverHttpError(status));
        }

        let token: TokenserverToken = resp.json()?;
        let server_timestamp = resp
            .headers
            .try_get::<ServerTimestamp, _>(header_names::X_TIMESTAMP)
            .ok_or(ErrorKind::MissingServerTimestamp)?;
        Ok(TokenFetchResult {
            token,
            server_timestamp,
        })
    }

    fn now(&self) -> SystemTime {
        SystemTime::now()
    }
}

// The context stored by our TokenProvider when it has a TokenState::Token
// state.
struct TokenContext {
    token: TokenserverToken,
    credentials: hawk::Credentials,
    server_timestamp: ServerTimestamp,
    valid_until: SystemTime,
}

// hawk::Credentials doesn't implement debug -_-
impl fmt::Debug for TokenContext {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> ::std::result::Result<(), fmt::Error> {
        f.debug_struct("TokenContext")
            .field("token", &self.token)
            .field("credentials", &"(omitted)")
            .field("server_timestamp", &self.server_timestamp)
            .field("valid_until", &self.valid_until)
            .finish()
    }
}

impl TokenContext {
    fn new(
        token: TokenserverToken,
        credentials: hawk::Credentials,
        server_timestamp: ServerTimestamp,
        valid_until: SystemTime,
    ) -> Self {
        Self {
            token,
            credentials,
            server_timestamp,
            valid_until,
        }
    }

    fn is_valid(&self, now: SystemTime) -> bool {
        // We could consider making the duration a little shorter - if it
        // only has 1 second validity there seems a reasonable chance it will
        // have expired by the time it gets presented to the remote that wants
        // it.
        // Either way though, we will eventually need to handle a token being
        // rejected as a non-fatal error and recover, so maybe we don't care?
        now < self.valid_until
    }

    fn authorization(&self, req: &Request) -> Result<String> {
        let url = &req.url;

        let path_and_query = match url.query() {
            None => Cow::from(url.path()),
            Some(qs) => Cow::from(format!("{}?{}", url.path(), qs)),
        };

        let host = url
            .host_str()
            .ok_or_else(|| ErrorKind::UnacceptableUrl("Storage URL has no host".into()))?;

        // Known defaults exist for https? (among others), so this should be impossible
        let port = url.port_or_known_default().ok_or_else(|| {
            ErrorKind::UnacceptableUrl(
                "Storage URL has no port and no default port is known for the protocol".into(),
            )
        })?;

        let header =
            hawk::RequestBuilder::new(req.method.as_str(), host, port, path_and_query.borrow())
                .request()
                .make_header(&self.credentials)?;

        Ok(format!("Hawk {}", header))
    }
}

// The state our TokenProvider holds to reflect the state of the token.
#[derive(Debug)]
enum TokenState {
    // We've never fetched a token.
    NoToken,
    // Have a token and last we checked it remained valid.
    Token(TokenContext),
    // We failed to fetch a token. First elt is the error, second elt is
    // the api_endpoint we had before we failed to fetch a new token (or
    // None if the very first attempt at fetching a token failed)
    Failed(Option<error::Error>, Option<String>),
    // Previously failed and told to back-off for SystemTime duration. Second
    // elt is the api_endpoint we had before we hit the backoff error.
    // XXX - should we roll Backoff and Failed together?
    Backoff(SystemTime, Option<String>),
    // api_endpoint changed - we are never going to get a token nor move out
    // of this state.
    NodeReassigned,
}

/// The generic TokenProvider implementation - long lived and fetches tokens
/// on demand (eg, when first needed, or when an existing one expires.)
#[derive(Debug)]
struct TokenProviderImpl<TF: TokenFetcher> {
    fetcher: TF,
    // Our token state (ie, whether we have a token, and if not, why not)
    current_state: RefCell<TokenState>,
}

impl<TF: TokenFetcher> TokenProviderImpl<TF> {
    fn new(fetcher: TF) -> Self {
        // We check this at the real entrypoint of the application, but tests
        // can/do bypass that, so check this here too.
        rc_crypto::ensure_initialized();
        TokenProviderImpl {
            fetcher,
            current_state: RefCell::new(TokenState::NoToken),
        }
    }

    // Uses our fetcher to grab a new token and if successful, derives other
    // info from that token into a usable TokenContext.
    fn fetch_context(&self) -> Result<TokenContext> {
        let result = self.fetcher.fetch_token()?;
        let token = result.token;
        let valid_until = SystemTime::now() + Duration::from_secs(token.duration);

        let credentials = hawk::Credentials {
            id: token.id.clone(),
            key: hawk::Key::new(token.key.as_bytes(), hawk::SHA256)?,
        };

        Ok(TokenContext::new(
            token,
            credentials,
            result.server_timestamp,
            valid_until,
        ))
    }

    // Attempt to fetch a new token and return a new state reflecting that
    // operation. If it worked a TokenState will be returned, but errors may
    // cause other states.
    fn fetch_token(&self, previous_endpoint: Option<&str>) -> TokenState {
        match self.fetch_context() {
            Ok(tc) => {
                // We got a new token - check that the endpoint is the same
                // as a previous endpoint we saw (if any)
                match previous_endpoint {
                    Some(prev) => {
                        if prev == tc.token.api_endpoint {
                            TokenState::Token(tc)
                        } else {
                            log::warn!(
                                "api_endpoint changed from {} to {}",
                                prev,
                                tc.token.api_endpoint
                            );
                            TokenState::NodeReassigned
                        }
                    }
                    None => {
                        // Never had an api_endpoint in the past, so this is OK.
                        TokenState::Token(tc)
                    }
                }
            }
            Err(e) => {
                // Early to avoid nll issues...
                if let ErrorKind::BackoffError(be) = e {
                    return TokenState::Backoff(be, previous_endpoint.map(ToString::to_string));
                }
                TokenState::Failed(Some(e), previous_endpoint.map(ToString::to_string))
            }
        }
    }

    // Given the state we are currently in, return a new current state.
    // Returns None if the current state should be used (eg, if we are
    // holding a token that remains valid) or Some() if the state has changed
    // (which may have changed to a state with a token or an error state)
    fn advance_state(&self, state: &TokenState) -> Option<TokenState> {
        match state {
            TokenState::NoToken => Some(self.fetch_token(None)),
            TokenState::Failed(_, existing_endpoint) => {
                Some(self.fetch_token(existing_endpoint.as_ref().map(String::as_str)))
            }
            TokenState::Token(existing_context) => {
                if existing_context.is_valid(self.fetcher.now()) {
                    None
                } else {
                    Some(self.fetch_token(Some(existing_context.token.api_endpoint.as_str())))
                }
            }
            TokenState::Backoff(ref until, ref existing_endpoint) => {
                if let Ok(remaining) = until.duration_since(self.fetcher.now()) {
                    log::debug!("enforcing existing backoff - {:?} remains", remaining);
                    None
                } else {
                    // backoff period is over
                    Some(self.fetch_token(existing_endpoint.as_ref().map(String::as_str)))
                }
            }
            TokenState::NodeReassigned => {
                // We never leave this state.
                None
            }
        }
    }

    fn with_token<T, F>(&self, func: F) -> Result<T>
    where
        F: FnOnce(&TokenContext) -> Result<T>,
    {
        // first get a mutable ref to our existing state, advance to the
        // state we will use, then re-stash that state for next time.
        let state: &mut TokenState = &mut self.current_state.borrow_mut();
        if let Some(new_state) = self.advance_state(state) {
            *state = new_state;
        }

        // Now re-fetch the state we should use for this call - if it's
        // anything other than TokenState::Token we will fail.
        match state {
            TokenState::NoToken => {
                // it should be impossible to get here.
                panic!("Can't be in NoToken state after advancing");
            }
            TokenState::Token(ref token_context) => {
                // make the call.
                func(token_context)
            }
            TokenState::Failed(e, _) => {
                // We swap the error out of the state enum and return it.
                Err(e.take().unwrap())
            }
            TokenState::NodeReassigned => {
                // this is unrecoverable.
                Err(ErrorKind::StorageResetError)
            }
            TokenState::Backoff(ref remaining, _) => Err(ErrorKind::BackoffError(*remaining)),
        }
    }

    fn hashed_uid(&self) -> Result<String> {
        self.with_token(|ctx| Ok(ctx.token.hashed_fxa_uid.clone()))
    }

    fn authorization(&self, req: &Request) -> Result<String> {
        self.with_token(|ctx| ctx.authorization(req))
    }

    fn api_endpoint(&self) -> Result<String> {
        self.with_token(|ctx| Ok(ctx.token.api_endpoint.clone()))
    }
    // TODO: we probably want a "drop_token/context" type method so that when
    // using a token with some validity fails the caller can force a new one
    // (in which case the new token request will probably fail with a 401)
}

// The public concrete object exposed by this module
#[derive(Debug)]
pub struct TokenProvider {
    imp: TokenProviderImpl<TokenServerFetcher>,
}

impl TokenProvider {
    pub fn new(url: Url, access_token: String, key_id: String) -> Self {
        let fetcher = TokenServerFetcher::new(url, access_token, key_id);
        Self {
            imp: TokenProviderImpl::new(fetcher),
        }
    }

    pub fn hashed_uid(&self) -> Result<String> {
        self.imp.hashed_uid()
    }

    pub fn authorization(&self, req: &Request) -> Result<String> {
        self.imp.authorization(req)
    }

    pub fn api_endpoint(&self) -> Result<String> {
        self.imp.api_endpoint()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    struct TestFetcher<FF, FN>
    where
        FF: Fn() -> Result<TokenFetchResult>,
        FN: Fn() -> SystemTime,
    {
        fetch: FF,
        now: FN,
    }
    impl<FF, FN> TokenFetcher for TestFetcher<FF, FN>
    where
        FF: Fn() -> Result<TokenFetchResult>,
        FN: Fn() -> SystemTime,
    {
        fn fetch_token(&self) -> Result<TokenFetchResult> {
            (self.fetch)()
        }
        fn now(&self) -> SystemTime {
            (self.now)()
        }
    }

    fn make_tsc<FF, FN>(fetch: FF, now: FN) -> Result<TokenProviderImpl<TestFetcher<FF, FN>>>
    where
        FF: Fn() -> Result<TokenFetchResult>,
        FN: Fn() -> SystemTime,
    {
        let fetcher: TestFetcher<FF, FN> = TestFetcher { fetch, now };
        Ok(TokenProviderImpl::new(fetcher))
    }

    #[test]
    fn test_endpoint() {
        nss::ensure_initialized();
        // Use a cell to avoid the closure having a mutable ref to this scope.
        let counter: Cell<u32> = Cell::new(0);
        let fetch = || {
            counter.set(counter.get() + 1);
            Ok(TokenFetchResult {
                token: TokenserverToken {
                    id: "id".to_string(),
                    key: "key".to_string(),
                    api_endpoint: "api_endpoint".to_string(),
                    uid: 1,
                    duration: 1000,
                    hashed_fxa_uid: "hash".to_string(),
                },
                server_timestamp: ServerTimestamp(0i64),
            })
        };

        let tsc = make_tsc(fetch, SystemTime::now).unwrap();

        let e = tsc.api_endpoint().expect("should work");
        assert_eq!(e, "api_endpoint".to_string());
        assert_eq!(counter.get(), 1);

        let e2 = tsc.api_endpoint().expect("should work");
        assert_eq!(e2, "api_endpoint".to_string());
        // should not have re-fetched.
        assert_eq!(counter.get(), 1);
    }

    #[test]
    fn test_backoff() {
        nss::ensure_initialized();
        let counter: Cell<u32> = Cell::new(0);
        let fetch = || {
            counter.set(counter.get() + 1);
            let when = SystemTime::now() + Duration::from_millis(10000);
            Err(ErrorKind::BackoffError(when))
        };
        let now: Cell<SystemTime> = Cell::new(SystemTime::now());
        let tsc = make_tsc(fetch, || now.get()).unwrap();

        tsc.api_endpoint().expect_err("should bail");
        // XXX - check error type.
        assert_eq!(counter.get(), 1);
        // try and get another token - should not re-fetch as backoff is still
        // in progress.
        tsc.api_endpoint().expect_err("should bail");
        assert_eq!(counter.get(), 1);

        // Advance the clock.
        now.set(now.get() + Duration::new(20, 0));

        // Our token fetch mock is still returning a backoff error, so we
        // still fail, but should have re-hit the fetch function.
        tsc.api_endpoint().expect_err("should bail");
        assert_eq!(counter.get(), 2);
    }

    #[test]
    fn test_validity() {
        nss::ensure_initialized();
        let counter: Cell<u32> = Cell::new(0);
        let fetch = || {
            counter.set(counter.get() + 1);
            Ok(TokenFetchResult {
                token: TokenserverToken {
                    id: "id".to_string(),
                    key: "key".to_string(),
                    api_endpoint: "api_endpoint".to_string(),
                    uid: 1,
                    duration: 10,
                    hashed_fxa_uid: "hash".to_string(),
                },
                server_timestamp: ServerTimestamp(0i64),
            })
        };
        let now: Cell<SystemTime> = Cell::new(SystemTime::now());
        let tsc = make_tsc(fetch, || now.get()).unwrap();

        tsc.api_endpoint().expect("should get a valid token");
        assert_eq!(counter.get(), 1);

        // try and get another token - should not re-fetch as the old one
        // remains valid.
        tsc.api_endpoint().expect("should reuse existing token");
        assert_eq!(counter.get(), 1);

        // Advance the clock.
        now.set(now.get() + Duration::new(20, 0));

        // We should discard our token and fetch a new one.
        tsc.api_endpoint().expect("should re-fetch");
        assert_eq!(counter.get(), 2);
    }

    #[test]
    fn test_server_url() {
        assert_eq!(
            fixup_server_url(
                Url::parse("https://token.services.mozilla.com/1.0/sync/1.5").unwrap()
            )
            .as_str(),
            "https://token.services.mozilla.com/1.0/sync/1.5"
        );
        assert_eq!(
            fixup_server_url(
                Url::parse("https://token.services.mozilla.com/1.0/sync/1.5/").unwrap()
            )
            .as_str(),
            "https://token.services.mozilla.com/1.0/sync/1.5"
        );
        assert_eq!(
            fixup_server_url(Url::parse("https://token.services.mozilla.com").unwrap()).as_str(),
            "https://token.services.mozilla.com/1.0/sync/1.5"
        );
        assert_eq!(
            fixup_server_url(Url::parse("https://token.services.mozilla.com/").unwrap()).as_str(),
            "https://token.services.mozilla.com/1.0/sync/1.5"
        );
        assert_eq!(
            fixup_server_url(
                Url::parse("https://selfhosted.example.com/token/1.0/sync/1.5").unwrap()
            )
            .as_str(),
            "https://selfhosted.example.com/token/1.0/sync/1.5"
        );
        assert_eq!(
            fixup_server_url(
                Url::parse("https://selfhosted.example.com/token/1.0/sync/1.5/").unwrap()
            )
            .as_str(),
            "https://selfhosted.example.com/token/1.0/sync/1.5"
        );
        assert_eq!(
            fixup_server_url(Url::parse("https://selfhosted.example.com/token/").unwrap()).as_str(),
            "https://selfhosted.example.com/token/1.0/sync/1.5"
        );
        assert_eq!(
            fixup_server_url(Url::parse("https://selfhosted.example.com/token").unwrap()).as_str(),
            "https://selfhosted.example.com/token/1.0/sync/1.5"
        );
    }
}
