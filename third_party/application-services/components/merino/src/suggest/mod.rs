/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod error;
mod http;
mod schema;
#[cfg(test)]
mod tests;

use error_support::handle_error;
use parking_lot::Mutex;
use url::Url;
use uuid::Uuid;

pub use error::{ApiResult, Error, MerinoSuggestApiError, Result};
pub use schema::{SuggestConfig, SuggestOptions};

const DEFAULT_BASE_HOST: &str = "https://merino.services.mozilla.com";

/// A client for the merino suggest endpoint.
///
/// Use [`SuggestClient::new`] to create an instance, then call
/// [`SuggestClient::get_suggestions`] to fetch suggestions for a query.
#[derive(uniffi::Object)]
pub struct SuggestClient {
    inner: SuggestClientInner<http::HttpClient>,
    endpoint_url: Url,
}

struct SuggestClientInner<T: http::HttpClientTrait> {
    http_client: T,
    session: Mutex<Option<SearchSession>>,
}

/// State for the current search session.
///
/// A session is started automatically when a request is about to be sent and
/// there is no active session. It lasts until [`SuggestClient::reset_session`]
/// is called.
struct SearchSession {
    id: String,
    sequence_number: u32,
}

impl SearchSession {
    fn new() -> Self {
        Self {
            id: Uuid::new_v4().to_string(),
            sequence_number: 0,
        }
    }
}

#[derive(Default)]
pub struct SuggestClientBuilder {
    base_host: Option<String>,
}

impl SuggestClientBuilder {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn base_host(mut self, base_host: String) -> Self {
        self.base_host = Some(base_host);
        self
    }

    pub fn build(self) -> Result<SuggestClient> {
        let base_host = self
            .base_host
            .unwrap_or_else(|| DEFAULT_BASE_HOST.to_string());

        let url = format!("{}/api/v1/suggest", base_host);
        let endpoint_url = Url::parse(&url)?;

        Ok(SuggestClient {
            inner: SuggestClientInner::new()?,
            endpoint_url,
        })
    }
}

#[uniffi::export]
impl SuggestClient {
    /// Creates a new `SuggestClient` from the given configuration.
    #[uniffi::constructor]
    #[handle_error(Error)]
    pub fn new(config: SuggestConfig) -> ApiResult<Self> {
        let mut builder = SuggestClientBuilder::new();

        if let Some(base_host) = config.base_host {
            builder = builder.base_host(base_host);
        }

        builder.build()
    }

    /// Fetches suggestions from the merino suggest endpoint for the given query.
    ///
    /// Returns the raw JSON response body as a string, or `None` if the server
    /// returned HTTP 204 (no suggestions available for weather).
    #[handle_error(Error)]
    pub fn get_suggestions(
        &self,
        query: String,
        options: SuggestOptions,
    ) -> ApiResult<Option<String>> {
        let response = self
            .inner
            .get_suggestions(query.as_str(), options, &self.endpoint_url)?;

        Ok(response.map(|r| r.text().to_string()))
    }

    /// Ends the current search session.
    ///
    /// A new session, with a new session ID and the sequence number reset to 0,
    /// is started on the next call to [`SuggestClient::get_suggestions`].
    ///
    /// Consumers should call this whenever a search session ends (e.g. the user
    /// navigates away from the address bar). Consumers may also call it to end a
    /// long-lived session after a certain duration for better privacy protection.
    pub fn reset_session(&self) {
        self.inner.reset_session();
    }
}

impl SuggestClientInner<http::HttpClient> {
    pub fn new() -> Result<Self> {
        Ok(Self {
            http_client: http::HttpClient,
            session: Mutex::new(None),
        })
    }
}

impl<T: http::HttpClientTrait> SuggestClientInner<T> {
    pub fn get_suggestions(
        &self,
        query: &str,
        options: SuggestOptions,
        endpoint_url: &Url,
    ) -> Result<Option<viaduct::Response>> {
        let providers = options
            .providers
            .filter(|v| !v.is_empty())
            .map(|v| v.join(","));
        let client_variants = options
            .client_variants
            .filter(|v| !v.is_empty())
            .map(|v| v.join(","));
        let (session_id, sequence_number) = self.advance_session();
        self.http_client.make_suggest_request(
            query,
            http::SuggestQueryParams {
                providers: providers.as_deref(),
                source: options.source.as_deref(),
                country: options.country.as_deref(),
                region: options.region.as_deref(),
                city: options.city.as_deref(),
                client_variants: client_variants.as_deref(),
                request_type: options.request_type.as_deref(),
                accept_language: options.accept_language.as_deref(),
                session_id: Some(session_id.as_str()),
                sequence_number: Some(sequence_number),
            },
            endpoint_url.clone(),
        )
    }

    pub fn reset_session(&self) {
        *self.session.lock() = None;
    }

    /// Advances the search session, starting a new one if there isn't one, and
    /// increments the sequence number.
    ///
    /// Returns the pre-advance session state: the session ID and the sequence
    /// number to send with the current request.
    fn advance_session(&self) -> (String, u32) {
        let mut session = self.session.lock();
        let session = session.get_or_insert_with(SearchSession::new);
        let params = (session.id.clone(), session.sequence_number);
        session.sequence_number += 1;
        params
    }
}

#[cfg(test)]
impl<T: http::HttpClientTrait> SuggestClientInner<T> {
    pub fn new_with_client(client: T) -> Self {
        Self {
            http_client: client,
            session: Mutex::new(None),
        }
    }
}
