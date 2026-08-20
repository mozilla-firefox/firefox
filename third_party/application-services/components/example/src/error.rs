/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Error handling for the component.
//!
//! Components generally create 2 error enums: one for internal usage and one for the public APIs.
//! The internal errors can have lots of variants and store any details that you might want to use.
//! The public errors can limit the number of variants to the ones consumers actually care about.
//!
//! In general, the Rust code will use the internal errors.  The public API, defined in `lib.rs`,
//! converts internal errors to public errors.

use error_support::{ErrorHandling, GetErrorHandling};

/// reexport helpers for logging.
pub use error_support::trace;

/// Result type for internal errors.  Since most code uses internal errors, we just call this one
/// `Result`.
pub type Result<T> = std::result::Result<T, Error>;

/// Result type for public errors.  The convention is to call this `ApiResult`.
pub type ApiResult<T> = std::result::Result<T, ApiError>;

/// Public error class
///
/// Make sure to derive `uniffi::Error` (https://mozilla.github.io/uniffi-rs/next/types/errors.html).
///
/// You probably also want to derive `thiserror::Error`, this allows you to define nice error
/// messages using the `#[error(...)]` attributes (https://docs.rs/thiserror/latest/thiserror/).
#[derive(Debug, thiserror::Error, uniffi::Error)]
pub enum ApiError {
    /// Network error while making an HTTP request
    ///
    /// This is a good type of error to expose to consumers, since they often may want to treat
    /// network errors differently -- for example by retrying after some time.
    #[error("Remote settings unexpected error: {reason}")]
    Network { reason: String },

    /// Everything else gets stuffed into the `Other` variant.  This often captures a many
    /// different kinds of errors and that's good, because it keeps things simple for the consumer.
    ///
    /// When dealing with a new kind of error, ask yourself if there's an action that consumers
    /// want to take specific to that error kind.  If not, just stuff it in `Other`
    #[error("Remote settings error: {reason}")]
    Other { reason: String },
}

/// Internal error class, typically just called `Error`.
///
/// Here we can have lots of variants, since it's all internal.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// Error opening the database.
    ///
    /// The `#[from]` attribute makes it so all `sql_support::open_database` errors will be
    /// automatically mapped to this variant.
    #[error("Error opening database: {0}")]
    OpenDatabase(#[from] sql_support::open_database::Error),
    #[error("Database error: {0}")]
    DatabaseError(#[from] rusqlite::Error),
    #[error("Interrupted")]
    Interrupted(#[from] interrupt_support::Interrupted),
    #[error("JSON Error: {0}")]
    JSONError(#[from] serde_json::Error),
    #[error("Error sending request: {0}")]
    RequestError(#[from] viaduct::ViaductError),
    #[error("Invalid URL: {0}")]
    InvalidUrl(#[from] url::ParseError),
    #[error("Error in HTTP response: {0}")]
    HttpError(String),
}

/// Define how our internal errors are handled and converted to external errors
///
/// This defines how internal errors get mapped to public ones.
/// This is also where we decide how to handle errors, should we log a warning or report it?
///
/// Reporting an error means that Firefox-Android will create a Sentry report for it.  We hope to
/// add support for error reporting on Desktop and iOS in the near future.
///
/// See `support/error/README.md` for how this works, especially the warning about PII.
impl GetErrorHandling for Error {
    /// Public Error type
    type ExternalError = ApiError;

    /// Define how to convert internal errors to public ones and also what kind of
    /// logging/reporting should we have
    fn get_error_handling(&self) -> ErrorHandling<Self::ExternalError> {
        match self {
            Self::RequestError(viaduct::ViaductError::NetworkError(e)) => {
                // Viaduct errors are converted to the `Network` variant
                ErrorHandling::convert(ApiError::Network {
                    reason: e.to_string(),
                })
                // Network errors are expected in practice.  Let's log, but not report them.
                .log_warning()
            }
            // Database errors are converted to `Other` and reported.
            //
            // The string passed to `report_error` controls how errors get grouped together in
            // Sentry.  By passing `example-component-database`, we ensure that database-related
            // errors get grouped into their own issue.
            Self::DatabaseError(_) | Self::OpenDatabase(_) => {
                ErrorHandling::convert(ApiError::Other {
                    reason: self.to_string(),
                })
                .report_error("example-component-database")
            }

            // All other error types are converted to `Other` and reported.
            //
            // This uses the `example-component-unexpected` slug.  In general, the volume for
            // unexpected errors should be low.  If you're seeing lots of errors then you have a
            // couple choices.
            //
            // If the errors are not really an issue, then change `report_error` to `log_warning`.
            //
            // If the errors are an issue, then take steps to try to better understand them:
            //
            // * Change the `report_error` slug to a different string so that they can be monitored separately.
            // * Add extra details to the error messages
            // * log breadcrumbs (see `db.rs` for an example of this).
            _ => ErrorHandling::convert(ApiError::Other {
                reason: self.to_string(),
            })
            .report_error("example-component-unexpected"),
        }
    }
}
