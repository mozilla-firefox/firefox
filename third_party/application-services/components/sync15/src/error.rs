/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use interrupt_support::Interrupted;

// reexport logging helpers.
#[allow(unused_imports)] // some only used with certain features.
pub use error_support::{debug, error, info, trace, warn};

/// This enum is to discriminate `StorageHttpError`, and not used as an error.
#[cfg(feature = "sync-client")]
#[derive(Debug, Clone)]
pub enum ErrorResponse {
    NotFound { route: String },
    // 401
    Unauthorized { route: String },
    // 412
    PreconditionFailed { route: String },
    // 5XX
    ServerError { route: String, status: u16 }, // TODO: info for "retry-after" and backoff handling etc here.
    // Other HTTP responses.
    RequestFailed { route: String, status: u16 },
}

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[cfg(feature = "crypto")]
    #[error("Key {0} had wrong length, got {1}, expected {2}")]
    BadKeyLength(&'static str, usize, usize),

    #[cfg(feature = "crypto")]
    #[error("SHA256 HMAC Mismatch error")]
    HmacMismatch,

    #[cfg(feature = "crypto")]
    #[error("Crypto/NSS error: {0}")]
    CryptoError(#[from] rc_crypto::Error),

    #[cfg(feature = "crypto")]
    #[error("Base64 decode error: {0}")]
    Base64Decode(#[from] base64::DecodeError),

    #[error("JSON error: {0}")]
    JsonError(#[from] serde_json::Error),

    #[error("Bad cleartext UTF8: {0}")]
    BadCleartextUtf8(#[from] std::string::FromUtf8Error),

    #[cfg(feature = "crypto")]
    #[error("HAWK error: {0}")]
    HawkError(#[from] rc_crypto::hawk::Error),

    //
    // Errors specific to this module.
    //
    #[cfg(feature = "sync-client")]
    #[error("HTTP status {0} when requesting a token from the tokenserver")]
    TokenserverHttpError(u16),

    #[cfg(feature = "sync-client")]
    #[error("HTTP storage error: {0:?}")]
    StorageHttpError(ErrorResponse),

    #[cfg(feature = "sync-client")]
    #[error("Server requested backoff. Retry after {0:?}")]
    BackoffError(std::time::SystemTime),

    #[cfg(feature = "sync-client")]
    #[error("Outgoing record is too large to upload")]
    RecordTooLargeError,

    // Do we want to record the concrete problems?
    #[cfg(feature = "sync-client")]
    #[error("Not all records were successfully uploaded")]
    RecordUploadFailed,

    /// Used for things like a node reassignment or an unexpected syncId
    /// implying the app needs to "reset" its understanding of remote storage.
    #[cfg(feature = "sync-client")]
    #[error("The server has reset the storage for this account")]
    StorageResetError,

    #[cfg(feature = "sync-client")]
    #[error("Unacceptable URL: {0}")]
    UnacceptableUrl(String),

    #[cfg(feature = "sync-client")]
    #[error("Missing server timestamp header in request")]
    MissingServerTimestamp,

    #[cfg(feature = "sync-client")]
    #[error("Unexpected server behavior during batch upload: {0}")]
    ServerBatchProblem(&'static str),

    #[cfg(feature = "sync-client")]
    #[error("It appears some other client is also trying to setup storage; try again later")]
    SetupRace,

    #[cfg(feature = "sync-client")]
    #[error("Client upgrade required; server data is in a newer format")]
    ClientUpgradeRequired,

    // This means that our global state machine needs to enter a state (such as
    // "FreshStartNeeded", but the allowed_states don't include that state.)
    // It typically means we are trying to do a "fast" or "read-only" sync.
    #[cfg(feature = "sync-client")]
    #[error("Our storage needs setting up and we can't currently do it")]
    SetupRequired,

    #[cfg(feature = "sync-client")]
    #[error("Store error: {0}")]
    StoreError(#[from] anyhow::Error),

    #[cfg(feature = "sync-client")]
    #[error("Network error: {0}")]
    RequestError(#[from] viaduct::ViaductError),

    #[cfg(feature = "sync-client")]
    #[error("Unexpected HTTP status: {0}")]
    UnexpectedStatus(#[from] viaduct::UnexpectedStatus),

    #[cfg(feature = "sync-client")]
    #[error("URL parse error: {0}")]
    MalformedUrl(#[from] url::ParseError),

    #[error("The operation was interrupted.")]
    Interrupted(#[from] Interrupted),
}

#[cfg(feature = "sync-client")]
impl Error {
    pub(crate) fn get_backoff(&self) -> Option<std::time::SystemTime> {
        if let Error::BackoffError(time) = self {
            Some(*time)
        } else {
            None
        }
    }
}
