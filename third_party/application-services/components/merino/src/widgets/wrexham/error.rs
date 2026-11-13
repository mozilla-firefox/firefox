/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use error_support::{ErrorHandling, GetErrorHandling};

pub type Result<T> = std::result::Result<T, Error>;

pub type ApiResult<T> = std::result::Result<T, MerinoWrexhamApiError>;

#[derive(Debug, thiserror::Error, uniffi::Error)]
pub enum MerinoWrexhamApiError {
    /// A network-level failure.
    #[error("Wrexham network error: {reason}")]
    Network { reason: String },

    /// Merino has no Wrexham data it can serve right now (HTTP 503). Try again later.
    #[error("Wrexham data unavailable: {reason}")]
    Unavailable { reason: String },

    /// Any other error, e.g. HTTP errors.
    #[error("Wrexham error: code {code:?}, reason: {reason}")]
    Other { code: Option<u16>, reason: String },
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// Failed to parse a URL.
    #[error("URL parse error: {0}")]
    UrlParse(#[from] url::ParseError),

    /// Failed to send the HTTP request.
    #[error("Error sending request: {0}")]
    Request(#[from] viaduct::ViaductError),

    /// The server rejected the request, e.g. an invalid `limit` (HTTP 400).
    #[error("Bad request ({code}): {message}")]
    BadRequest { code: u16, message: String },

    /// Merino can't read the Wrexham data right now (HTTP 503).
    #[error("Unavailable ({code}): {message}")]
    Unavailable { code: u16, message: String },

    /// The server encountered another error (HTTP 5xx).
    #[error("Server error ({code}): {message}")]
    Server { code: u16, message: String },

    /// An unexpected HTTP status code was received.
    #[error("Unexpected error ({code}): {message}")]
    Unexpected { code: u16, message: String },
}

impl GetErrorHandling for Error {
    type ExternalError = MerinoWrexhamApiError;

    fn get_error_handling(&self) -> ErrorHandling<Self::ExternalError> {
        match self {
            Self::Request { .. } => ErrorHandling::convert(MerinoWrexhamApiError::Network {
                reason: self.to_string(),
            })
            .log_warning(),

            // Expected while Merino's Wrexham data is unavailable, and every client would hit
            // it at once, so it's logged rather than reported.
            Self::Unavailable { .. } => {
                ErrorHandling::convert(MerinoWrexhamApiError::Unavailable {
                    reason: self.to_string(),
                })
                .log_warning()
            }

            Self::BadRequest { code, .. }
            | Self::Server { code, .. }
            | Self::Unexpected { code, .. } => {
                ErrorHandling::convert(MerinoWrexhamApiError::Other {
                    code: Some(*code),
                    reason: self.to_string(),
                })
                .report_error("merino-http-error")
            }

            Self::UrlParse(_) => ErrorHandling::convert(MerinoWrexhamApiError::Other {
                code: None,
                reason: self.to_string(),
            })
            .report_error("merino-unexpected"),
        }
    }
}
