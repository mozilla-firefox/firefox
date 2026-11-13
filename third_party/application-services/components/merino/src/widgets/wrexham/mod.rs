/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod error;
mod http;
mod schema;
#[cfg(test)]
mod tests;

use error_support::handle_error;
use url::Url;

use crate::DEFAULT_BASE_HOST;

pub use error::{ApiResult, Error, MerinoWrexhamApiError, Result};
pub use schema::{WrexhamConfig, WrexhamMatchesOptions};

/// A client for the merino Wrexham widget endpoints.
///
/// Use [`WrexhamClient::new`] to create an instance, then call
/// [`WrexhamClient::get_matches`] or [`WrexhamClient::get_standings`].
#[derive(uniffi::Object)]
pub struct WrexhamClient {
    inner: WrexhamClientInner<http::HttpClient>,
    base_url: Url,
}

struct WrexhamClientInner<T: http::HttpClientTrait> {
    http_client: T,
}

#[derive(Default)]
pub struct WrexhamClientBuilder {
    base_host: Option<String>,
}

impl WrexhamClientBuilder {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn base_host(mut self, base_host: String) -> Self {
        self.base_host = Some(base_host);
        self
    }

    pub fn build(self) -> Result<WrexhamClient> {
        Ok(WrexhamClient {
            inner: WrexhamClientInner::new(),
            base_url: base_url(self.base_host.as_deref())?,
        })
    }
}

/// Returns the URL the Wrexham endpoints live under, ending with a `/` so endpoint names can be
/// joined onto it.
fn base_url(base_host: Option<&str>) -> Result<Url> {
    let base_host = base_host.unwrap_or(DEFAULT_BASE_HOST).trim_end_matches('/');
    Ok(Url::parse(&format!("{base_host}/api/v1/widgets/wrexham/"))?)
}

#[uniffi::export]
impl WrexhamClient {
    /// Creates a new `WrexhamClient` from the given configuration.
    #[uniffi::constructor]
    #[handle_error(Error)]
    pub fn new(config: WrexhamConfig) -> ApiResult<Self> {
        let mut builder = WrexhamClientBuilder::new();

        if let Some(base_host) = config.base_host {
            builder = builder.base_host(base_host);
        }

        builder.build()
    }

    /// Fetches Wrexham's previous, current and upcoming matches.
    ///
    /// Returns the raw JSON response body as a string.
    #[handle_error(Error)]
    pub fn get_matches(&self, options: WrexhamMatchesOptions) -> ApiResult<String> {
        self.inner.get_matches(&self.base_url, options)
    }

    /// Fetches the EFL Championship table.
    ///
    /// Returns the raw JSON response body as a string.
    #[handle_error(Error)]
    pub fn get_standings(&self) -> ApiResult<String> {
        self.inner.get_standings(&self.base_url)
    }
}

impl WrexhamClientInner<http::HttpClient> {
    pub fn new() -> Self {
        Self {
            http_client: http::HttpClient,
        }
    }
}

impl<T: http::HttpClientTrait> WrexhamClientInner<T> {
    pub fn get_matches(&self, base_url: &Url, options: WrexhamMatchesOptions) -> Result<String> {
        let url = http::build_matches_url(base_url.join("matches")?, options.limit);
        let response = self.http_client.make_request(url)?;
        Ok(response.text().to_string())
    }

    pub fn get_standings(&self, base_url: &Url) -> Result<String> {
        let response = self.http_client.make_request(base_url.join("standings")?)?;
        Ok(response.text().to_string())
    }
}

#[cfg(test)]
impl<T: http::HttpClientTrait> WrexhamClientInner<T> {
    pub fn new_with_client(client: T) -> Self {
        Self {
            http_client: client,
        }
    }
}
