/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::*;
use url::Url;
use viaduct::{Headers, Method, Response};

const SAMPLE_RESPONSE: &str = r#"{
  "suggestions": [
    {
      "title": "Wikipedia - Apple Inc.",
      "url": "https://en.wikipedia.org/wiki/Apple_Inc.",
      "provider": "wikipedia",
      "is_sponsored": false,
      "score": 0.23,
      "icon": "https://merino-images.services.mozilla.com/favicons/4c8bf96d667fa2e9f072bdd8e9f25c8ba6ba2ad55df1af7d9ea0dd575c12abee_1313.png",
      "categories": [6],
      "full_keyword": "apple",
      "advertiser": "dynamic-wikipedia",
      "block_id": 0
    }
  ],
  "request_id": "938abd549272454c8fc7b8615b57d34a",
  "client_variants": [],
  "server_variants": []
}"#;

fn make_response(status: u16, body: &str) -> Response {
    Response {
        request_method: Method::Get,
        url: Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap(),
        status,
        headers: Headers::new(),
        body: body.as_bytes().to_vec(),
    }
}

fn default_options() -> SuggestOptions {
    SuggestOptions {
        providers: None,
        source: None,
        country: None,
        region: None,
        city: None,
        client_variants: None,
        request_type: None,
        accept_language: None,
    }
}

struct FakeHttpClientSuccess;

impl http::HttpClientTrait for FakeHttpClientSuccess {
    fn make_suggest_request(
        &self,
        _query: &str,
        _options: http::SuggestQueryParams<'_>,
        _endpoint_url: Url,
    ) -> Result<Option<Response>> {
        Ok(Some(make_response(200, SAMPLE_RESPONSE)))
    }
}

struct FakeHttpClientValidationError;

impl http::HttpClientTrait for FakeHttpClientValidationError {
    fn make_suggest_request(
        &self,
        _query: &str,
        _options: http::SuggestQueryParams<'_>,
        _endpoint_url: Url,
    ) -> Result<Option<Response>> {
        Err(Error::Validation {
            code: 422,
            message: "Invalid input".to_string(),
        })
    }
}

struct FakeHttpClientServerError;

impl http::HttpClientTrait for FakeHttpClientServerError {
    fn make_suggest_request(
        &self,
        _query: &str,
        _options: http::SuggestQueryParams<'_>,
        _endpoint_url: Url,
    ) -> Result<Option<Response>> {
        Err(Error::Server {
            code: 500,
            message: "Internal server error".to_string(),
        })
    }
}

struct FakeHttpClientBadRequestError;

impl http::HttpClientTrait for FakeHttpClientBadRequestError {
    fn make_suggest_request(
        &self,
        _query: &str,
        _options: http::SuggestQueryParams<'_>,
        _endpoint_url: Url,
    ) -> Result<Option<Response>> {
        Err(Error::BadRequest {
            code: 400,
            message: "Bad request".to_string(),
        })
    }
}

struct FakeHttpClientNoContent;

impl http::HttpClientTrait for FakeHttpClientNoContent {
    fn make_suggest_request(
        &self,
        _query: &str,
        _options: http::SuggestQueryParams<'_>,
        _endpoint_url: Url,
    ) -> Result<Option<Response>> {
        Ok(None)
    }
}

struct FakeCapturingClient {
    captured_url: std::sync::Arc<std::sync::Mutex<Option<Url>>>,
}

impl http::HttpClientTrait for FakeCapturingClient {
    fn make_suggest_request(
        &self,
        _query: &str,
        _options: http::SuggestQueryParams<'_>,
        endpoint_url: Url,
    ) -> Result<Option<Response>> {
        *self.captured_url.lock().unwrap() = Some(endpoint_url);
        Ok(Some(make_response(200, "{}")))
    }
}

struct FakeCapturingClientWithParams {
    captured_url: std::sync::Arc<std::sync::Mutex<Option<Url>>>,
}

impl http::HttpClientTrait for FakeCapturingClientWithParams {
    fn make_suggest_request(
        &self,
        query: &str,
        options: http::SuggestQueryParams<'_>,
        mut endpoint_url: Url,
    ) -> Result<Option<Response>> {
        {
            let mut params = endpoint_url.query_pairs_mut();
            params.append_pair("q", query);
            if let Some(v) = options.providers {
                params.append_pair("providers", v);
            }
            if let Some(v) = options.client_variants {
                params.append_pair("client_variants", v);
            }
        }
        *self.captured_url.lock().unwrap() = Some(endpoint_url);
        Ok(Some(make_response(200, "{}")))
    }
}

#[test]
fn test_get_suggestions_success() {
    let client_inner = SuggestClientInner::new_with_client(FakeHttpClientSuccess);
    let result = client_inner.get_suggestions(
        "apple",
        default_options(),
        &Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap(),
    );

    assert!(result.is_ok());
    assert_eq!(result.unwrap().unwrap().text(), SAMPLE_RESPONSE);
}

#[test]
fn test_get_suggestions_no_content() {
    let client_inner = SuggestClientInner::new_with_client(FakeHttpClientNoContent);
    let result = client_inner.get_suggestions(
        "apple",
        default_options(),
        &Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap(),
    );

    assert!(result.is_ok());
    assert!(result.unwrap().is_none());
}

#[test]
fn test_get_suggestions_validation_error() {
    let client_inner = SuggestClientInner::new_with_client(FakeHttpClientValidationError);
    let result = client_inner.get_suggestions(
        "apple",
        default_options(),
        &Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap(),
    );

    assert!(result.is_err());
    match result.unwrap_err() {
        Error::Validation { code, message } => {
            assert_eq!(code, 422);
            assert_eq!(message, "Invalid input");
        }
        _ => panic!("Expected a validation error"),
    }
}

#[test]
fn test_get_suggestions_server_error() {
    let client_inner = SuggestClientInner::new_with_client(FakeHttpClientServerError);
    let result = client_inner.get_suggestions(
        "apple",
        default_options(),
        &Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap(),
    );

    assert!(result.is_err());
    match result.unwrap_err() {
        Error::Server { code, message } => {
            assert_eq!(code, 500);
            assert_eq!(message, "Internal server error");
        }
        _ => panic!("Expected a server error"),
    }
}

#[test]
fn test_get_suggestions_bad_request_error() {
    let client_inner = SuggestClientInner::new_with_client(FakeHttpClientBadRequestError);
    let result = client_inner.get_suggestions(
        "apple",
        default_options(),
        &Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap(),
    );

    assert!(result.is_err());
    match result.unwrap_err() {
        Error::BadRequest { code, message } => {
            assert_eq!(code, 400);
            assert_eq!(message, "Bad request");
        }
        _ => panic!("Expected a bad request error"),
    }
}

#[test]
fn test_builder_uses_default_base_host() {
    let captured_url = std::sync::Arc::new(std::sync::Mutex::new(None));
    let client_inner = SuggestClientInner::new_with_client(FakeCapturingClient {
        captured_url: captured_url.clone(),
    });

    let client = SuggestClientBuilder::new().build().unwrap();
    let _ = client_inner.get_suggestions("apple", default_options(), &client.endpoint_url);

    let captured = captured_url.lock().unwrap();
    assert_eq!(
        captured.as_ref().unwrap().as_str(),
        "https://merino.services.mozilla.com/api/v1/suggest"
    );
}

#[test]
fn test_builder_uses_custom_base_host() {
    let captured_url = std::sync::Arc::new(std::sync::Mutex::new(None));
    let client_inner = SuggestClientInner::new_with_client(FakeCapturingClient {
        captured_url: captured_url.clone(),
    });

    let client = SuggestClientBuilder::new()
        .base_host("https://stage.merino.services.mozilla.com".to_string())
        .build()
        .unwrap();
    let _ = client_inner.get_suggestions("apple", default_options(), &client.endpoint_url);

    let captured = captured_url.lock().unwrap();
    assert_eq!(
        captured.as_ref().unwrap().as_str(),
        "https://stage.merino.services.mozilla.com/api/v1/suggest"
    );
}

#[test]
fn test_providers_and_client_variants_joined_as_comma_separated() {
    let captured_url = std::sync::Arc::new(std::sync::Mutex::new(None));
    let client_inner = SuggestClientInner::new_with_client(FakeCapturingClientWithParams {
        captured_url: captured_url.clone(),
    });

    let options = SuggestOptions {
        providers: Some(vec![
            "wikipedia".to_string(),
            "adm".to_string(),
            "accuweather".to_string(),
        ]),
        client_variants: Some(vec!["control".to_string(), "treatment".to_string()]),
        ..default_options()
    };

    let endpoint = Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap();
    let _ = client_inner.get_suggestions("apple", options, &endpoint);

    let captured = captured_url.lock().unwrap();
    let url = captured.as_ref().unwrap();
    let params: std::collections::HashMap<_, _> = url.query_pairs().into_owned().collect();

    assert_eq!(params["providers"], "wikipedia,adm,accuweather");
    assert_eq!(params["client_variants"], "control,treatment");
}

#[test]
fn test_empty_providers_and_client_variants_omitted() {
    let captured_url = std::sync::Arc::new(std::sync::Mutex::new(None));
    let client_inner = SuggestClientInner::new_with_client(FakeCapturingClientWithParams {
        captured_url: captured_url.clone(),
    });

    let options = SuggestOptions {
        providers: Some(vec![]),
        client_variants: Some(vec![]),
        ..default_options()
    };

    let endpoint = Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap();
    let _ = client_inner.get_suggestions("apple", options, &endpoint);

    let captured = captured_url.lock().unwrap();
    let url = captured.as_ref().unwrap();
    let params: std::collections::HashMap<_, _> = url.query_pairs().into_owned().collect();

    assert!(!params.contains_key("providers"));
    assert!(!params.contains_key("client_variants"));
}

type CapturedSession = (Option<String>, Option<u32>);

struct FakeSessionCapturingClient {
    captured: std::sync::Arc<std::sync::Mutex<Vec<CapturedSession>>>,
    status: u16,
}

impl http::HttpClientTrait for FakeSessionCapturingClient {
    fn make_suggest_request(
        &self,
        _query: &str,
        options: http::SuggestQueryParams<'_>,
        _endpoint_url: Url,
    ) -> Result<Option<Response>> {
        self.captured.lock().unwrap().push((
            options.session_id.map(str::to_string),
            options.sequence_number,
        ));
        match self.status {
            200 => Ok(Some(make_response(200, "{}"))),
            code => Err(Error::Server {
                code,
                message: "Internal server error".to_string(),
            }),
        }
    }
}

fn session_capturing_client(
    status: u16,
) -> (
    SuggestClientInner<FakeSessionCapturingClient>,
    std::sync::Arc<std::sync::Mutex<Vec<CapturedSession>>>,
) {
    let captured = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
    let client_inner = SuggestClientInner::new_with_client(FakeSessionCapturingClient {
        captured: captured.clone(),
        status,
    });
    (client_inner, captured)
}

#[test]
fn test_session_started_on_first_request() {
    let (client_inner, captured) = session_capturing_client(200);
    let endpoint = Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap();

    let _ = client_inner.get_suggestions("a", default_options(), &endpoint);

    let captured = captured.lock().unwrap();
    let (sid, seq) = &captured[0];
    let sid = sid.as_deref().expect("sid should be sent");
    assert!(uuid::Uuid::parse_str(sid).is_ok(), "sid should be a UUID");
    assert_eq!(*seq, Some(0));
}

#[test]
fn test_sequence_number_increments_within_session() {
    let (client_inner, captured) = session_capturing_client(200);
    let endpoint = Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap();

    for query in ["a", "ap", "app"] {
        let _ = client_inner.get_suggestions(query, default_options(), &endpoint);
    }

    let captured = captured.lock().unwrap();
    let sid = captured[0].0.clone();
    assert!(captured.iter().all(|(s, _)| *s == sid));
    let seqs: Vec<_> = captured.iter().map(|(_, seq)| *seq).collect();
    assert_eq!(seqs, vec![Some(0), Some(1), Some(2)]);
}

#[test]
fn test_sequence_number_increments_on_failed_request() {
    let (client_inner, captured) = session_capturing_client(500);
    let endpoint = Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap();

    let _ = client_inner.get_suggestions("a", default_options(), &endpoint);
    let _ = client_inner.get_suggestions("ap", default_options(), &endpoint);

    let captured = captured.lock().unwrap();
    assert_eq!(captured[0].0, captured[1].0);
    assert_eq!(captured[0].1, Some(0));
    assert_eq!(captured[1].1, Some(1));
}

#[test]
fn test_reset_session_starts_new_session() {
    let (client_inner, captured) = session_capturing_client(200);
    let endpoint = Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap();

    let _ = client_inner.get_suggestions("a", default_options(), &endpoint);
    let _ = client_inner.get_suggestions("ap", default_options(), &endpoint);
    client_inner.reset_session();
    let _ = client_inner.get_suggestions("b", default_options(), &endpoint);

    let captured = captured.lock().unwrap();
    assert_ne!(captured[0].0, captured[2].0);
    assert_eq!(captured[2].1, Some(0));
}

#[test]
fn test_reset_session_without_active_session() {
    let (client_inner, captured) = session_capturing_client(200);
    let endpoint = Url::parse("https://merino.services.mozilla.com/api/v1/suggest").unwrap();

    client_inner.reset_session();
    let _ = client_inner.get_suggestions("a", default_options(), &endpoint);

    let captured = captured.lock().unwrap();
    assert!(captured[0].0.is_some());
    assert_eq!(captured[0].1, Some(0));
}

#[test]
fn test_builder_fails_with_invalid_base_host() {
    let result = SuggestClientBuilder::new()
        .base_host("not a valid url".to_string())
        .build();

    match result {
        Err(Error::UrlParse(_)) => {}
        Err(other) => panic!("Expected UrlParse error, got: {:?}", other),
        Ok(_) => panic!("Expected error for invalid base_host"),
    }
}
