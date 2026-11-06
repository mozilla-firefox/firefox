/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use serde::Deserialize;
use std::hash::{Hash, Hasher};
use url::Url;
use viaduct::{Headers, Request, GLOBAL_SETTINGS};

use super::error::CallbackRequestError;
use crate::non_empty_string::NonEmptyString;

pub struct PreflightRequest(pub Url);

impl Hash for PreflightRequest {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.as_str().hash(state);
    }
}

impl From<PreflightRequest> for Request {
    fn from(req: PreflightRequest) -> Self {
        Request::get(req.0)
    }
}

/// Response from the MARS `/v1/ads-preflight` endpoint.
#[derive(Debug, Deserialize)]
pub struct PreflightResponse {
    #[serde(default)]
    pub geo_location: String,
    #[serde(default, deserialize_with = "NonEmptyString::deserialize_option")]
    pub normalized_ua: Option<NonEmptyString>,
}

impl PreflightResponse {
    pub fn into_headers(
        self,
        browser_user_agent: Option<&NonEmptyString>,
    ) -> Result<Headers, CallbackRequestError> {
        let user_agent = self
            .normalized_ua
            .or_else(|| browser_user_agent.cloned())
            .or_else(|| {
                GLOBAL_SETTINGS
                    .read()
                    .default_user_agent
                    .clone()
                    .and_then(NonEmptyString::new)
            })
            .ok_or(CallbackRequestError::MissingUserAgent)?;
        let mut headers = Headers::new();
        headers.insert("X-Geo-Location", self.geo_location)?;
        headers.insert("X-User-Agent", user_agent)?;
        Ok(headers)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn preflight(geo_location: &str, normalized_ua: &str) -> PreflightResponse {
        serde_json::from_value(serde_json::json!({
            "geo_location": geo_location,
            "normalized_ua": normalized_ua,
        }))
        .unwrap()
    }

    fn browser_user_agent() -> Option<NonEmptyString> {
        NonEmptyString::new("Mozilla/5.0 Firefox/140.0")
    }

    #[test]
    fn normalized_ua_wins_over_the_fallback() {
        let headers = preflight("US-CA", "Firefox/140.0")
            .into_headers(browser_user_agent().as_ref())
            .unwrap();

        assert_eq!(headers.get("X-Geo-Location"), Some("US-CA"));
        assert_eq!(headers.get("X-User-Agent"), Some("Firefox/140.0"));
    }

    #[test]
    fn empty_normalized_ua_falls_back_to_the_browser_user_agent() {
        let headers = preflight("US-CA", "")
            .into_headers(browser_user_agent().as_ref())
            .unwrap();

        assert_eq!(
            headers.get("X-User-Agent"),
            Some("Mozilla/5.0 Firefox/140.0")
        );
    }

    #[test]
    fn falls_back_to_viaducts_user_agent_else_is_an_error() {
        GLOBAL_SETTINGS.write().default_user_agent = None;
        let result = preflight("US-CA", "").into_headers(None);
        assert!(matches!(
            result,
            Err(CallbackRequestError::MissingUserAgent)
        ));

        viaduct::settings::set_global_default_user_agent("viaduct-user-agent".into());
        let headers = preflight("US-CA", "").into_headers(None).unwrap();
        GLOBAL_SETTINGS.write().default_user_agent = None;
        assert_eq!(headers.get("X-User-Agent"), Some("viaduct-user-agent"));
    }

    #[test]
    fn invalid_header_values_are_an_error_not_a_panic() {
        assert!(preflight("Zürich", "Firefox/140.0")
            .into_headers(None)
            .is_err());
        assert!(preflight("US-CA", "Firefox/140.0\r\nX-Injected: yes")
            .into_headers(None)
            .is_err());
    }
}
