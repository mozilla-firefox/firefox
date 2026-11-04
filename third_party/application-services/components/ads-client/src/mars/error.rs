/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use sql_support::open_database;
use viaduct::Response;

#[derive(Debug, thiserror::Error)]
pub enum BuildRequestError {
    #[error("Duplicate placement_id found: {placement_id}. Placement_ids must be unique.")]
    DuplicatePlacementId { placement_id: String },

    #[error("Could not build request with empty placement configs")]
    EmptyConfig,
}

#[derive(Debug, thiserror::Error)]
pub enum CallbackRequestError {
    #[error("Could not fetch ads, MARS responded with: {0}")]
    HTTPError(#[from] HTTPError),

    #[error("Invalid callback URL: {0}")]
    InvalidUrl(#[from] url::ParseError),

    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("Error sending request: {0}")]
    Request(#[from] viaduct::ViaductError),
}

#[derive(Debug, thiserror::Error)]
pub enum FetchAdsError {
    #[error("Error building ad request: {0}")]
    BuildRequest(#[from] BuildRequestError),

    #[error("Could not fetch ads, MARS responded with: {0}")]
    HTTPError(#[from] HTTPError),

    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("OHTTP preflight failed: {0}")]
    Preflight(#[from] CallbackRequestError),

    #[error("Error sending request: {0}")]
    Request(#[from] viaduct::ViaductError),

    #[error("Internal database error: {0}")]
    Sqlite(#[from] open_database::Error),

    #[error("URL parse error: {0}")]
    UrlParse(#[from] url::ParseError),
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

impl HTTPError {
    pub(crate) fn check(response: &Response) -> Result<(), Self> {
        let status = response.status;

        if status == 200 {
            return Ok(());
        }
        let error_message = response.text();
        let error = match status {
            400 => Self::BadRequest {
                code: status,
                message: error_message.to_string(),
            },
            500..=599 => Self::Server {
                code: status,
                message: error_message.to_string(),
            },
            _ => Self::Unexpected {
                code: status,
                message: error_message.to_string(),
            },
        };
        Err(error)
    }
}

#[derive(Debug, thiserror::Error)]
pub enum RecordClickError {
    #[error("Callback request to MARS failed: {0}")]
    CallbackRequest(#[from] CallbackRequestError),
}

#[derive(Debug, thiserror::Error)]
pub enum RecordImpressionError {
    #[error("Callback request to MARS failed: {0}")]
    CallbackRequest(#[from] CallbackRequestError),
}

#[derive(Debug, thiserror::Error)]
pub enum ReportAdError {
    #[error("Callback request to MARS failed: {0}")]
    CallbackRequest(#[from] CallbackRequestError),
}

#[derive(Debug, thiserror::Error)]
pub(crate) enum TransportError {
    #[error("HTTP error: {0}")]
    Http(#[from] HTTPError),

    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("Request error: {0}")]
    Request(#[from] viaduct::ViaductError),
}

impl From<TransportError> for FetchAdsError {
    fn from(err: TransportError) -> Self {
        match err {
            TransportError::Http(e) => Self::HTTPError(e),
            TransportError::Json(e) => Self::Json(e),
            TransportError::Request(e) => Self::Request(e),
        }
    }
}

impl From<TransportError> for CallbackRequestError {
    fn from(err: TransportError) -> Self {
        match err {
            TransportError::Http(e) => Self::HTTPError(e),
            TransportError::Json(e) => Self::Json(e),
            TransportError::Request(e) => Self::Request(e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use url::Url;

    fn mock_response(status: u16, body: &str) -> Response {
        Response {
            body: body.as_bytes().to_vec(),
            headers: viaduct::Headers::new(),
            request_method: viaduct::Method::Get,
            status,
            url: Url::parse("https://example.com").unwrap(),
        }
    }

    #[test]
    fn test_ok_status_returns_ok() {
        let response = mock_response(200, "OK");
        let result = HTTPError::check(&response);
        assert!(result.is_ok());
    }

    #[test]
    fn test_bad_request_returns_http_error() {
        let response = mock_response(400, "Bad input");
        let result = HTTPError::check(&response);
        assert!(
            matches!(result, Err(HTTPError::BadRequest { code, message }) if code == 400 && message == "Bad input")
        );
    }

    #[test]
    fn test_server_error_500() {
        let response = mock_response(500, "Something broke");
        let result = HTTPError::check(&response);
        assert!(
            matches!(result, Err(HTTPError::Server { code, message }) if code == 500 && message == "Something broke")
        );
    }
}
