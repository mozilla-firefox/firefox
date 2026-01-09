/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use error_support::error;
use viaduct::Response;

#[derive(Debug, thiserror::Error)]
pub enum ComponentError {
    #[error("Error requesting ads: {0}")]
    RequestAds(#[from] RequestAdsError),

    #[error("Error recording a click for a placement: {0}")]
    RecordClick(#[from] RecordClickError),

    #[error("Error recording an impressions for a placement: {0}")]
    RecordImpression(#[from] RecordImpressionError),

    #[error("Error reporting an ad: {0}")]
    ReportAd(#[from] ReportAdError),
}

#[derive(Debug, thiserror::Error)]
pub enum RequestAdsError {
    #[error("Error building ad requests from configs: {0}")]
    BuildRequest(#[from] BuildRequestError),

    #[error(transparent)]
    ContextId(#[from] context_id::ApiError),

    #[error("Error requesting ads from MARS: {0}")]
    FetchAds(#[from] FetchAdsError),
}

#[derive(Debug, thiserror::Error)]
pub enum BuildRequestError {
    #[error("Could not build request with empty placement configs")]
    EmptyConfig,

    #[error("Duplicate placement_id found: {placement_id}. Placement_ids must be unique.")]
    DuplicatePlacementId { placement_id: String },
}

#[derive(Debug, thiserror::Error)]
pub enum FetchAdsError {
    #[error("URL parse error: {0}")]
    UrlParse(#[from] url::ParseError),

    #[error("Error sending request: {0}")]
    Request(#[from] viaduct::ViaductError),

    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("Could not fetch ads, MARS responded with: {0}")]
    HTTPError(#[from] HTTPError),
}

#[derive(Debug, thiserror::Error)]
pub enum CallbackRequestError {
    #[error("Could not fetch ads, MARS responded with: {0}")]
    HTTPError(#[from] HTTPError),

    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("Invalid callback URL: {0}")]
    InvalidUrl(#[from] url::ParseError),

    #[error("Error sending request: {0}")]
    Request(#[from] viaduct::ViaductError),
}

#[derive(Debug, thiserror::Error)]
pub enum RecordImpressionError {
    #[error("Callback request to MARS failed: {0}")]
    CallbackRequest(#[from] CallbackRequestError),
}

#[derive(Debug, thiserror::Error)]
pub enum RecordClickError {
    #[error("Callback request to MARS failed: {0}")]
    CallbackRequest(#[from] CallbackRequestError),
}

#[derive(Debug, thiserror::Error)]
pub enum ReportAdError {
    #[error("Callback request to MARS failed: {0}")]
    CallbackRequest(#[from] CallbackRequestError),
}

#[derive(Debug, thiserror::Error)]
pub enum HTTPError {
    #[error("Bad request ({code}): {message}")]
    BadRequest { code: u16, message: String },

    #[error("Server error ({code}): {message}")]
    Server { code: u16, message: String },

    #[error("Unexpected error ({code}): {message}")]
    Unexpected { code: u16, message: String },
}

pub fn check_http_status_for_error(response: &Response) -> Result<(), HTTPError> {
    let status = response.status;

    if status == 200 {
        return Ok(());
    }
    let error_message = response.text();
    let error = match status {
        400 => HTTPError::BadRequest {
            code: status,
            message: error_message.to_string(),
        },
        500..=599 => HTTPError::Server {
            code: status,
            message: error_message.to_string(),
        },
        _ => HTTPError::Unexpected {
            code: status,
            message: error_message.to_string(),
        },
    };
    Err(error)
}

#[cfg(test)]
mod tests {
    use super::*;
    use url::Url;

    fn mock_response(status: u16, body: &str) -> Response {
        Response {
            request_method: viaduct::Method::Get,
            url: Url::parse("https://example.com").unwrap(),
            status,
            headers: viaduct::Headers::new(),
            body: body.as_bytes().to_vec(),
        }
    }

    #[test]
    fn test_ok_status_returns_ok() {
        let response = mock_response(200, "OK");
        let result = check_http_status_for_error(&response);
        assert!(result.is_ok());
    }

    #[test]
    fn test_bad_request_returns_http_error() {
        let response = mock_response(400, "Bad input");
        let result = check_http_status_for_error(&response);
        assert!(
            matches!(result, Err(HTTPError::BadRequest { code, message }) if code == 400 && message == "Bad input")
        );
    }

    #[test]
    fn test_server_error_500() {
        let response = mock_response(500, "Something broke");
        let result = check_http_status_for_error(&response);
        assert!(
            matches!(result, Err(HTTPError::Server { code, message }) if code == 500 && message == "Something broke")
        );
    }
}
