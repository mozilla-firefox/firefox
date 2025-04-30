/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use error_support::{ErrorHandling, GetErrorHandling};

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
    FileError(#[from] std::io::Error),
    /// An error has occurred while sending a request.
    #[error("Error sending request: {0}")]
    RequestError(#[from] viaduct::Error),
    /// An error has occurred while parsing an URL.
    #[error("Error parsing URL: {0}")]
    UrlParsingError(#[from] url::ParseError),
    /// The server has asked the client to backoff.
    #[error("Server asked the client to back off ({0} seconds remaining)")]
    BackoffError(u64),
    /// The server returned an error code or the response was unexpected.
    #[error("Error in network response: {0}")]
    ResponseError(String),
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
}

// Define how our internal errors are handled and converted to external errors
// See `support/error/README.md` for how this works, especially the warning about PII.
impl GetErrorHandling for Error {
    type ExternalError = RemoteSettingsError;

    fn get_error_handling(&self) -> ErrorHandling<Self::ExternalError> {
        match self {
            // Network errors are expected to happen in practice.  Let's log, but not report them.
            Self::RequestError(viaduct::Error::NetworkError(e)) => {
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
            .report_error("logins-unexpected"),
        }
    }
}
