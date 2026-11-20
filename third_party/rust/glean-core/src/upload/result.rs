// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/// The result of an attempted ping upload.
#[derive(Debug)]
pub enum UploadResult {
    /// A recoverable failure.
    ///
    /// During upload something went wrong,
    /// e.g. the network connection failed.
    /// The upload should be retried at a later time.
    RecoverableFailure {
        #[doc(hidden)]
        /// Unused field. Required because UniFFI can't handle variants without fields.
        unused: i8,
    },

    /// An unrecoverable upload failure.
    ///
    /// A possible cause might be a malformed URL.
    UnrecoverableFailure {
        #[doc(hidden)]
        /// Unused field. Required because UniFFI can't handle variants without fields.
        unused: i8,
    },

    /// The uploader is not capable of uploading this request due to lack of or
    /// mismatched capabilities.
    ///
    /// e.g. The ping requires upload over OHTTP, but the uploader doesn't support OHTTP.
    Incapable {
        #[doc(hidden)]
        /// Unused field. Required because UniFFI can't handle variants without fields.
        unused: i8,
    },

    /// A HTTP response code.
    ///
    /// This can still indicate an error, depending on the status code.
    HttpStatus {
        /// The HTTP status code
        code: i32,
    },

    /// Signal that this uploader is done with work
    /// and won't accept new work.
    Done {
        #[doc(hidden)]
        /// Unused field. Required because UniFFI can't handle variants without fields.
        unused: i8,
    },
}

impl UploadResult {
    /// Gets the label to be used in recording error counts for upload.
    ///
    /// Returns `None` if the upload finished succesfully.
    /// Failures are recorded in the `ping_upload_failure` metric.
    pub fn get_label(&self) -> Option<&str> {
        match self {
            UploadResult::HttpStatus { code: 200..=299 } => None,
            UploadResult::HttpStatus { code: 408 } => Some("status_code_408"),
            UploadResult::HttpStatus { code: 429 } => Some("status_code_429"),
            UploadResult::HttpStatus { code: 400..=499 } => Some("status_code_4xx"),
            UploadResult::HttpStatus { code: 500..=599 } => Some("status_code_5xx"),
            UploadResult::HttpStatus { .. } => Some("status_code_unknown"),
            UploadResult::UnrecoverableFailure { .. } => Some("unrecoverable"),
            UploadResult::RecoverableFailure { .. } => Some("recoverable"),
            UploadResult::Incapable { .. } => Some("incapable"),
            UploadResult::Done { .. } => None,
        }
    }

    /// A recoverable failure.
    ///
    /// During upload something went wrong,
    /// e.g. the network connection failed.
    /// The upload should be retried at a later time.
    pub fn recoverable_failure() -> Self {
        Self::RecoverableFailure { unused: 0 }
    }

    /// An unrecoverable upload failure.
    ///
    /// A possible cause might be a malformed URL.
    pub fn unrecoverable_failure() -> Self {
        Self::UnrecoverableFailure { unused: 0 }
    }

    /// The uploader is not capable of uploading this request due to lack of or
    /// mismatched capabilities.
    ///
    /// e.g. The ping requires upload over OHTTP, but the uploader doesn't support OHTTP.
    pub fn incapable() -> Self {
        Self::Incapable { unused: 0 }
    }

    /// A HTTP response code.
    ///
    /// This can still indicate an error, depending on the status code.
    pub fn http_status(code: i32) -> Self {
        Self::HttpStatus { code }
    }

    /// This uploader is done.
    pub fn done() -> Self {
        Self::Done { unused: 0 }
    }
}

/// Communication back whether the uploader loop should continue.
#[derive(Debug)]
pub enum UploadTaskAction {
    /// Instruct the caller to continue with work.
    Next,
    /// Instruct the caller to end work.
    End,
}
