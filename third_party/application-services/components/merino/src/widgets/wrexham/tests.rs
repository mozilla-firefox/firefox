/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::*;
use error_support::GetErrorHandling;
use parking_lot::Mutex;
use url::Url;
use viaduct::{Headers, Method, Response, ViaductError};

const MATCHES_RESPONSE: &str = r#"{"previous": [], "current": [], "upcoming": []}"#;
const STANDINGS_RESPONSE: &str =
    r#"{"competition": {"id": 34, "name": "EFL Championship"}, "standings": []}"#;
const UNAVAILABLE_RESPONSE: &str = r#"{"statusCode": 503, "message": "Wrexham data is temporarily unavailable. Please try again later."}"#;

/// Returns a canned response, run through the real status checks, and records the requested
/// URLs.
struct FakeHttpClient {
    status: u16,
    body: &'static str,
    requested_urls: Mutex<Vec<Url>>,
}

impl FakeHttpClient {
    fn new(status: u16, body: &'static str) -> Self {
        Self {
            status,
            body,
            requested_urls: Mutex::new(Vec::new()),
        }
    }

    fn last_url(&self) -> String {
        self.requested_urls.lock().last().unwrap().to_string()
    }
}

impl http::HttpClientTrait for FakeHttpClient {
    fn make_request(&self, url: Url) -> Result<Response> {
        self.requested_urls.lock().push(url.clone());
        http::check_status(Response {
            request_method: Method::Get,
            url,
            status: self.status,
            headers: Headers::new(),
            body: self.body.as_bytes().to_vec(),
        })
    }
}

struct FakeNetworkError;

impl http::HttpClientTrait for FakeNetworkError {
    fn make_request(&self, _url: Url) -> Result<Response> {
        Err(Error::Request(ViaductError::NetworkError(
            "offline".to_string(),
        )))
    }
}

fn default_base_url() -> Url {
    base_url(None).unwrap()
}

#[test]
fn test_get_matches_returns_the_response_body() {
    let client = WrexhamClientInner::new_with_client(FakeHttpClient::new(200, MATCHES_RESPONSE));

    let body = client
        .get_matches(&default_base_url(), WrexhamMatchesOptions::default())
        .unwrap();

    assert_eq!(body, MATCHES_RESPONSE);
    assert_eq!(
        client.http_client.last_url(),
        "https://merino.services.mozilla.com/api/v1/widgets/wrexham/matches"
    );
}

#[test]
fn test_get_matches_sends_the_limit() {
    let client = WrexhamClientInner::new_with_client(FakeHttpClient::new(200, MATCHES_RESPONSE));

    client
        .get_matches(
            &default_base_url(),
            WrexhamMatchesOptions { limit: Some(2) },
        )
        .unwrap();

    assert_eq!(
        client.http_client.last_url(),
        "https://merino.services.mozilla.com/api/v1/widgets/wrexham/matches?limit=2"
    );
}

#[test]
fn test_get_standings_returns_the_response_body() {
    let client = WrexhamClientInner::new_with_client(FakeHttpClient::new(200, STANDINGS_RESPONSE));

    let body = client.get_standings(&default_base_url()).unwrap();

    assert_eq!(body, STANDINGS_RESPONSE);
    assert_eq!(
        client.http_client.last_url(),
        "https://merino.services.mozilla.com/api/v1/widgets/wrexham/standings"
    );
}

#[test]
fn test_unavailable_response_returns_unavailable() {
    let client =
        WrexhamClientInner::new_with_client(FakeHttpClient::new(503, UNAVAILABLE_RESPONSE));

    let matches = client.get_matches(&default_base_url(), WrexhamMatchesOptions::default());
    let standings = client.get_standings(&default_base_url());

    assert!(matches!(matches, Err(Error::Unavailable { code: 503, .. })));
    assert!(matches!(
        standings,
        Err(Error::Unavailable { code: 503, .. })
    ));
}

#[test]
fn test_bad_request_returns_bad_request() {
    let client = WrexhamClientInner::new_with_client(FakeHttpClient::new(400, "bad limit"));

    let result = client.get_matches(
        &default_base_url(),
        WrexhamMatchesOptions { limit: Some(9) },
    );

    assert!(matches!(result, Err(Error::BadRequest { code: 400, .. })));
}

#[test]
fn test_network_error_returns_request_error() {
    let client = WrexhamClientInner::new_with_client(FakeNetworkError);

    let result = client.get_standings(&default_base_url());

    assert!(matches!(result, Err(Error::Request(_))));
}

#[test]
fn test_errors_convert_to_api_errors() {
    let unavailable = Error::Unavailable {
        code: 503,
        message: UNAVAILABLE_RESPONSE.to_string(),
    };
    let network = Error::Request(ViaductError::NetworkError("offline".to_string()));
    let server = Error::Server {
        code: 500,
        message: "boom".to_string(),
    };

    assert!(matches!(
        unavailable.get_error_handling().err,
        MerinoWrexhamApiError::Unavailable { .. }
    ));
    assert!(matches!(
        network.get_error_handling().err,
        MerinoWrexhamApiError::Network { .. }
    ));
    assert!(matches!(
        server.get_error_handling().err,
        MerinoWrexhamApiError::Other {
            code: Some(500),
            ..
        }
    ));
}

#[test]
fn test_base_url_uses_the_production_host_by_default() {
    assert_eq!(
        default_base_url().as_str(),
        "https://merino.services.mozilla.com/api/v1/widgets/wrexham/"
    );
}

#[test]
fn test_base_url_uses_a_custom_host() {
    let url = base_url(Some("http://localhost:8000/")).unwrap();

    assert_eq!(
        url.as_str(),
        "http://localhost:8000/api/v1/widgets/wrexham/"
    );
}

#[test]
fn test_builder_fails_with_an_invalid_base_host() {
    let result = WrexhamClientBuilder::new()
        .base_host("not a url".to_string())
        .build();

    assert!(matches!(result, Err(Error::UrlParse(_))));
}
