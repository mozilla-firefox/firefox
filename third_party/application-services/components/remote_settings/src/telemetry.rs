/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{fmt, sync::Arc};

use crate::error::Error;

/// Remote Settings sync status.
#[derive(Debug, PartialEq, uniffi::Enum)]
pub enum SyncStatus {
    /// Sync completed and new data was stored.
    Success,
    /// Local data is already up to date, no new data was stored.
    UpToDate,
    /// A network-level error occurred (connection refused, timeout, bad HTTP status, ...)
    NetworkError,
    /// The server asked the client to back off.
    BackoffError,
    /// Content signature verification failed.
    SignatureError,
    /// The certificate chain URL is unreachable or has invalid content-type.
    CertificateError,
    /// Server error (5xx status)
    ServerError,
    /// An unknown error occurred.
    UnknownError,
}

impl fmt::Display for SyncStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            SyncStatus::Success => "success",
            SyncStatus::UpToDate => "up_to_date",
            SyncStatus::NetworkError => "network_error",
            SyncStatus::BackoffError => "backoff_error",
            // We align the telemetry value with Desktop to simplify queries.
            SyncStatus::SignatureError => "sign_retry_error",
            SyncStatus::CertificateError => "certificate_error",
            SyncStatus::ServerError => "server_error",
            SyncStatus::UnknownError => "unknown_error",
        };
        f.write_str(s)
    }
}

#[derive(Debug, PartialEq, uniffi::Record, Default)]
pub struct UptakeEventExtras {
    /// Main sync status.
    pub value: Option<String>,
    /// Source of the sync (eg. "settings-changes-monitoring", "main/{collection}", ...)
    pub source: Option<String>,
    /// Age of the data in milliseconds, if available.
    pub age: Option<String>,
    /// Trigger that caused the sync (eg. "manual", "startup", "scheduled", ...) if available.
    pub trigger: Option<String>,
    /// Timestamp received from the server, if available.
    pub timestamp: Option<String>,
    /// Duration of the sync operation in milliseconds, if available.
    pub duration: Option<String>,
    /// The name of the error that occurred, if available.
    pub error_name: Option<String>,
}

/// Trait implemented by consumers to record Remote Settings metrics with Glean.
///
/// Consumers should implement this trait and pass it to
/// [crate::RemoteSettingsService::set_telemetry].
///
/// Consumers implement the trait like this (Kotlin example):
/// ```kotlin
/// /* Import the UniFFI-generated bindings */
/// import mozilla.appservices.remote_settings.RemoteSettingsTelemetry
/// import mozilla.appservices.remote_settings.UptakeEventExtras
/// /* Import the Glean-generated bindings */
/// import org.mozilla.appservices.remote_settings.GleanMetrics.RemoteSettings as RSMetrics
///
/// class GleanTelemetry : RemoteSettingsTelemetry {
///     override fun report_uptake(eventExtras: UptakeEventExtras) {
///         RSMetrics.uptakeRemotesettings.record(eventExtras)
///     }
/// }
///
/// service.setTelemetry(GleanTelemetry())
/// ```
#[cfg_attr(feature = "telemetry-submission", uniffi::export(with_foreign))]
pub trait RemoteSettingsTelemetry: Send + Sync {
    /// Report uptake event.
    fn report_uptake(&self, extras: UptakeEventExtras);
}

#[cfg(not(feature = "glean-sym"))]
struct NoopRemoteSettingsTelemetry;

#[cfg(not(feature = "glean-sym"))]
impl RemoteSettingsTelemetry for NoopRemoteSettingsTelemetry {
    fn report_uptake(&self, _extras: UptakeEventExtras) {}
}

#[cfg(feature = "glean-sym")]
struct GleanSymTelemetry;

#[cfg(feature = "glean-sym")]
impl RemoteSettingsTelemetry for GleanSymTelemetry {
    fn report_uptake(&self, extras: UptakeEventExtras) {
        use crate::glean_metrics::remote_settings::{
            uptake_remotesettings, UptakeRemotesettingsExtra,
        };
        uptake_remotesettings.record(UptakeRemotesettingsExtra {
            value: extras.value,
            source: extras.source,
            age: extras.age,
            trigger: extras.trigger,
            timestamp: extras.timestamp,
            duration: extras.duration,
            errorname: extras.error_name,
        });
    }
}

/// Wrapper around [RemoteSettingsTelemetry] used internally.
#[derive(Clone)]
pub struct RemoteSettingsTelemetryWrapper {
    inner: Arc<dyn RemoteSettingsTelemetry>,
}

impl Default for RemoteSettingsTelemetryWrapper {
    fn default() -> Self {
        // Record telemetry directly from Rust.
        // [crate::RemoteSettingsService::set_telemetry] will be ignored.
        #[cfg(feature = "glean-sym")]
        let inner = Arc::new(GleanSymTelemetry);
        // Otherwise, set to no-op untils the application calls
        // [crate::RemoteSettingsService::set_telemetry].
        #[cfg(not(feature = "glean-sym"))]
        let inner = Arc::new(NoopRemoteSettingsTelemetry);
        Self::new(inner)
    }
}

impl RemoteSettingsTelemetryWrapper {
    pub fn new(inner: Arc<dyn RemoteSettingsTelemetry>) -> Self {
        Self { inner }
    }

    pub fn report_uptake_success(&self, source: &str, duration: Option<u64>) {
        self.inner.report_uptake(UptakeEventExtras {
            value: Some(SyncStatus::Success.to_string()),
            source: Some(source.to_string()),
            age: None,
            trigger: None,
            timestamp: None,
            duration: duration.map(|d| d.to_string()),
            error_name: None,
        });
    }

    pub fn report_uptake_up_to_date(&self, source: &str, duration: Option<u64>) {
        self.inner.report_uptake(UptakeEventExtras {
            value: Some(SyncStatus::UpToDate.to_string()),
            source: Some(source.to_string()),
            age: None,
            trigger: None,
            timestamp: None,
            duration: duration.map(|d| d.to_string()),
            error_name: None,
        });
    }

    pub fn report_uptake_error(&self, error: &Error, source: &str) {
        // This is a bit hacky and naive, but it allows us to get the original
        // error type without needing to add too much machinery to our error types.
        // This mimics what we do in the desktop client:
        // https://searchfox.org/firefox-main/rev/26c440c6196eb0b4/services/settings/RemoteSettingsClient.sys.mjs#965
        let error_name = format!("{error:?}")
            .split(&['{', '('])
            .next()
            .unwrap_or("")
            .trim()
            .to_string();

        self.inner.report_uptake(UptakeEventExtras {
            value: Some(error_to_status(error).to_string()),
            source: Some(source.to_string()),
            age: None,
            trigger: None,
            timestamp: None,
            duration: None,
            error_name: Some(error_name),
        });
    }
}

fn error_to_status(error: &Error) -> SyncStatus {
    match error {
        Error::RequestError(viaduct::ViaductError::NetworkError(_))
        | Error::ResponseError { .. } => SyncStatus::NetworkError,
        Error::BackoffError(_) => SyncStatus::BackoffError,
        #[cfg(feature = "signatures")]
        Error::IncompleteSignatureDataError(_) => SyncStatus::SignatureError,
        #[cfg(feature = "signatures")]
        Error::SignatureError(_) => SyncStatus::SignatureError,
        Error::CertificateError(_) => SyncStatus::CertificateError,
        _ => SyncStatus::UnknownError,
    }
}
