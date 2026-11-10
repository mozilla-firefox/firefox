/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use error_support::{ErrorHandling, GetErrorHandling};
// reexport logging helpers.
pub use error_support::{breadcrumb, debug, error, info, trace, warn};

pub type ApiResult<T> = std::result::Result<T, RemoteSettingsError>;
pub type Result<T> = std::result::Result<T, Error>;

/// Public error class, this is what we return to consumers
#[derive(Debug, thiserror::Error, uniffi::Error)]
pub enum RemoteSettingsError {
    /// Network error while making a remote settings request
    #[error("Remote settings unexpected error: {reason}")]
    Network { reason: String },

    /// The server has asked the client to backoff.
    #[error("Server asked the client to back off ({seconds} seconds remaining)")]
    Backoff { seconds: u64 },

    #[error("Remote settings error: {reason}")]
    Other { reason: String },
}

/// Internal error class, this is what we use inside this crate
#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("Error opening database: {0}")]
    OpenDatabase(#[from] sql_support::open_database::Error),
    #[error("JSON Error: {0}")]
    JSONError(#[from] serde_json::Error),
    #[error("Error writing downloaded attachment: {0}")]
    AttachmentFileError(std::io::Error),
    #[error("Error creating storage dir: {0}")]
    CreateDirError(std::io::Error),
    /// An error has occurred while sending a request.
    #[error("Error sending request: {0}")]
    RequestError(#[from] viaduct::ViaductError),
    /// An error has occurred while parsing an URL.
    #[error("Error parsing URL: {0}")]
    UrlParsingError(#[from] url::ParseError),
    /// The server has asked the client to backoff.
    #[error("Server asked the client to back off ({0} seconds remaining)")]
    BackoffError(u64),
    /// The server returned an error code or the response was unexpected.
    #[error("Error in network response: {message} ({url})")]
    ResponseError { url: String, message: String },
    #[error("This server doesn't support attachments")]
    AttachmentsUnsupportedError,
    #[error("Error configuring client: {0}")]
    ConfigError(String),
    #[error("Database error: {0}")]
    DatabaseError(#[from] rusqlite::Error),
    #[error("Database closed")]
    DatabaseClosed,
    #[error("No attachment in given record: {0}")]
    RecordAttachmentMismatchError(String),
    #[error("Incomplete signature data: {0}")]
    IncompleteSignatureDataError(String),
    #[cfg(feature = "signatures")]
    #[error("Data could not be serialized: {0}")]
    SerializationError(#[from] canonical_json::CanonicalJSONError),
    #[cfg(feature = "signatures")]
    #[error("Signature could not be verified: {0}")]
    SignatureError(#[from] rc_crypto::Error),
    /// The certificate chain URL is unreachable or has invalid content-type.
    #[error("Certificate chain could not be fetched: {0}")]
    CertificateError(Box<Error>),
}

/// Error payload returned by the Remote Settings server (Kinto).
///
/// See https://docs.kinto-storage.org/en/stable/api/1.x/errors.html
#[derive(serde::Deserialize)]
struct ServerError {
    errno: u16,
    error: String,
    message: Option<String>,
    details: Option<serde_json::Value>,
}

impl Error {
    pub fn response_error(resp: &viaduct::Response) -> Self {
        let status = resp.status;
        // Responses from the Remote Settings server have a JSON body describing the error.
        // Other hosts (e.g. the attachments CDN) don't, so we only report the status code.
        let message = match resp.json::<ServerError>() {
            Ok(ServerError {
                errno,
                error,
                message: server_message,
                details,
            }) => {
                let mut message = format!("status code: {status} - {error} (errno {errno})");
                if let Some(server_message) = server_message {
                    message.push_str(&format!(": {server_message}"));
                }
                // `details` is arbitrary JSON, e.g. a list of validation errors or the id of a
                // missing record.
                match details {
                    None | Some(serde_json::Value::Null) => {}
                    Some(serde_json::Value::Array(ref a)) if a.is_empty() => {}
                    Some(details) => message.push_str(&format!(" - details: {details}")),
                }
                message
            }
            Err(_) => format!("status code: {status}"),
        };
        Self::ResponseError {
            url: resp.url.to_string(),
            message,
        }
    }
}

// Define how our internal errors are handled and converted to external errors
// See `support/error/README.md` for how this works, especially the warning about PII.
impl GetErrorHandling for Error {
    type ExternalError = RemoteSettingsError;

    fn get_error_handling(&self) -> ErrorHandling<Self::ExternalError> {
        match self {
            // Network errors are expected to happen in practice.  Let's log, but not report them.
            Self::RequestError(viaduct::ViaductError::NetworkError(e)) => {
                ErrorHandling::convert(RemoteSettingsError::Network {
                    reason: e.to_string(),
                })
                .log_warning()
            }
            // Backoff error shouldn't happen in practice, so let's report them for now.
            // If these do happen in practice and we decide that there is a valid reason for them,
            // then consider switching from reporting to Sentry to counting in Glean.
            Self::BackoffError(seconds) => {
                ErrorHandling::convert(RemoteSettingsError::Backoff { seconds: *seconds })
                    .report_error("suggest-backoff")
            }
            _ => ErrorHandling::convert(RemoteSettingsError::Other {
                reason: self.to_string(),
            })
            .report_error("remote-settings-unexpected"),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use viaduct::{Headers, Method, Response};

    fn response(status: u16, body: &str) -> Response {
        Response {
            request_method: Method::Get,
            url: "https://example.com/v2/".parse().unwrap(),
            status,
            headers: Headers::new(),
            body: body.as_bytes().to_vec(),
        }
    }

    #[test]
    fn test_response_error_server_error() {
        let resp = response(
            400,
            r#"{"code": 400, "errno": 107, "error": "Invalid parameters", "message": "_expected in querystring: Required", "details": [{"location": "querystring", "name": "_expected", "description": "Required"}]}"#,
        );
        assert_eq!(
            Error::response_error(&resp).to_string(),
            r#"Error in network response: status code: 400 - Invalid parameters (errno 107): _expected in querystring: Required - details: [{"location":"querystring","name":"_expected","description":"Required"}] (https://example.com/v2/)"#,
        );
    }

    // `message` is optional in the server's error payload.
    #[test]
    fn test_response_error_server_error_without_message() {
        let resp = response(
            404,
            r#"{"code": 404, "errno": 110, "error": "Not Found", "details": {"id": "abc", "resource_name": "record"}}"#,
        );
        assert_eq!(
            Error::response_error(&resp).to_string(),
            r#"Error in network response: status code: 404 - Not Found (errno 110) - details: {"id":"abc","resource_name":"record"} (https://example.com/v2/)"#,
        );
    }

    // `details` is optional in the server's error payload.
    #[test]
    fn test_response_error_server_error_without_details() {
        let resp = response(
            503,
            r#"{"code": 503, "errno": 201, "error": "Service Unavailable", "message": "Service temporary unavailable due to overloading or maintenance, please retry later."}"#,
        );
        assert_eq!(
            Error::response_error(&resp).to_string(),
            "Error in network response: status code: 503 - Service Unavailable (errno 201): Service temporary unavailable due to overloading or maintenance, please retry later. (https://example.com/v2/)",
        );
    }

    #[test]
    fn test_response_error_server_error_empty_details() {
        for details in ["null", "[]"] {
            let resp = response(
                400,
                &format!(
                    r#"{{"code": 400, "errno": 107, "error": "Invalid parameters", "message": "Bad request", "details": {details}}}"#
                ),
            );
            assert_eq!(
                Error::response_error(&resp).to_string(),
                "Error in network response: status code: 400 - Invalid parameters (errno 107): Bad request (https://example.com/v2/)",
                "details: {details}",
            );
        }
    }

    // Responses from other hosts, e.g. the attachments CDN.
    #[test]
    fn test_response_error_non_json_body() {
        let resp = response(
            404,
            "<?xml version='1.0' encoding='UTF-8'?><Error><Code>NoSuchKey</Code></Error>",
        );
        assert_eq!(
            Error::response_error(&resp).to_string(),
            "Error in network response: status code: 404 (https://example.com/v2/)",
        );
    }

    #[test]
    fn test_response_error_non_server_json_body() {
        let resp = response(502, "{}");
        assert_eq!(
            Error::response_error(&resp).to_string(),
            "Error in network response: status code: 502 (https://example.com/v2/)",
        );
    }
}
