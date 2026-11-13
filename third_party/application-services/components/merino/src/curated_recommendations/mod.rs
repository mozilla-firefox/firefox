/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

//! Client for fetching curated recommendations from the Merino service.
//!
//! This module provides [`CuratedRecommendationsClient`], which makes HTTP requests to the
//! Merino backend API to retrieve curated content recommendations. The client is configured
//! with a [`CuratedRecommendationsConfig`] and returns [`CuratedRecommendationsResponse`]
//! containing recommended articles, feeds, and layout information.

mod error;
mod http;
pub mod models;
#[cfg(test)]
mod tests;

pub use crate::curated_recommendations::models::locale::CuratedRecommendationLocale;
use crate::curated_recommendations::models::request::CuratedRecommendationsConfig;
use crate::curated_recommendations::models::request::CuratedRecommendationsRequest;
use crate::curated_recommendations::models::response::CuratedRecommendationsResponse;
pub use error::{ApiResult, CuratedRecommendationsApiError, Error, Result};
use error_support::handle_error;
use url::Url;

/// Default base host for the Merino curated recommendations API.
const DEFAULT_BASE_HOST: &str = "https://merino.services.mozilla.com";

/// Client for fetching curated recommendations from the Merino service.
///
/// Construct using [`CuratedRecommendationsClient::new`] with a
/// [`CuratedRecommendationsConfig`], then call
/// [`get_curated_recommendations`](CuratedRecommendationsClient::get_curated_recommendations)
/// to fetch recommendations.
#[derive(uniffi::Object)]
pub struct CuratedRecommendationsClient {
    inner: CuratedRecommendationsClientInner<http::HttpClient>,
    endpoint_url: Url,
    user_agent_header: String,
}

/// Internal client wrapper that is generic over the HTTP implementation,
/// enabling dependency injection of fake HTTP clients in tests.
struct CuratedRecommendationsClientInner<T: http::HttpClientTrait> {
    http_client: T,
}

/// Builder for constructing a [`CuratedRecommendationsClient`] with optional configuration.
///
/// If no `base_host` is provided, the client defaults to the production Merino service.
/// A `user_agent_header` is required.
#[derive(Default)]
pub struct CuratedRecommendationsClientBuilder {
    base_host: Option<String>,
    user_agent_header: Option<String>,
}

impl CuratedRecommendationsClientBuilder {
    /// Creates a new builder with no configuration set.
    pub fn new() -> Self {
        Self {
            base_host: None,
            user_agent_header: None,
        }
    }

    /// Sets a custom base host URL for the Merino API (e.g. for staging environments).
    pub fn base_host(mut self, base_host: impl Into<String>) -> Self {
        self.base_host = Some(base_host.into());
        self
    }

    /// Sets the `User-Agent` header to include in API requests.
    pub fn user_agent_header(mut self, user_agent_header: impl Into<String>) -> Self {
        self.user_agent_header = Some(user_agent_header.into());
        self
    }

    /// Builds the [`CuratedRecommendationsClient`].
    ///
    /// Returns an error if `user_agent_header` was not set or if the resulting URL is invalid.
    pub fn build(self) -> Result<CuratedRecommendationsClient> {
        let user_agent_header = self.user_agent_header.ok_or_else(|| Error::Unexpected {
            code: 0,
            message: "user_agent_header must be provided".to_string(),
        })?;

        let base_host = self
            .base_host
            .unwrap_or_else(|| DEFAULT_BASE_HOST.to_string());

        let url = format!("{}/api/v1/curated-recommendations", base_host);
        let endpoint_url = Url::parse(&url)?;

        Ok(CuratedRecommendationsClient {
            inner: CuratedRecommendationsClientInner::new()?,
            endpoint_url,
            user_agent_header,
        })
    }
}

#[uniffi::export]
impl CuratedRecommendationsClient {
    /// Creates a new client from the given configuration.
    #[uniffi::constructor]
    #[handle_error(Error)]
    pub fn new(config: CuratedRecommendationsConfig) -> ApiResult<Self> {
        let mut builder =
            CuratedRecommendationsClientBuilder::new().user_agent_header(config.user_agent_header);

        if let Some(base_host) = config.base_host {
            builder = builder.base_host(base_host);
        }

        builder.build()
    }

    /// Fetches curated recommendations from the Merino API.
    #[handle_error(Error)]
    pub fn get_curated_recommendations(
        &self,
        request: &CuratedRecommendationsRequest,
    ) -> ApiResult<CuratedRecommendationsResponse> {
        self.inner
            .get_curated_recommendations(request, &self.user_agent_header, &self.endpoint_url)
    }
}
/// Parses a serialized locale string (e.g. `"en-US"`) into a `CuratedRecommendationLocale` enum variant.
///
///
/// Returns `None` if the string does not match any known locale.
#[uniffi::export]
pub fn curated_recommendation_locale_from_string(
    locale: String,
) -> Option<CuratedRecommendationLocale> {
    CuratedRecommendationLocale::from_locale_string(locale)
}

/// Returns a list of all supported locale strings that map to `CuratedRecommendationLocale` variants.
#[uniffi::export]
pub fn all_curated_recommendation_locales() -> Vec<String> {
    CuratedRecommendationLocale::all_locales()
}

impl CuratedRecommendationsClientInner<http::HttpClient> {
    pub fn new() -> Result<Self> {
        Ok(Self {
            http_client: http::HttpClient,
        })
    }
}

impl<T: http::HttpClientTrait> CuratedRecommendationsClientInner<T> {
    pub fn get_curated_recommendations(
        &self,
        request: &CuratedRecommendationsRequest,
        user_agent_header: &str,
        endpoint_url: &Url,
    ) -> Result<CuratedRecommendationsResponse> {
        self.http_client.make_curated_recommendation_request(
            request,
            user_agent_header,
            endpoint_url.clone(),
        )
    }
}

#[cfg(test)]
impl<T: http::HttpClientTrait> CuratedRecommendationsClientInner<T> {
    // allows us to inject a fake http client for testing
    pub fn new_with_client(client: T) -> Self {
        Self {
            http_client: client,
        }
    }
}
