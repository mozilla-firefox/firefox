/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

//! The telemetry backend: one function per metric in `metrics.yaml`, each
//! forwarded to the process-wide [`Telemetry`] implementation.
//!
//! The function signatures match the glean-sym backend this will be replaced
//! with, so the swap only touches this file. [`Telemetry`], [`install`] and
//! [`uninstall`] exist only because the FFI layer hands us a UniFFI callback
//! that has to be dropped again before shutdown; glean-sym needs none of them.
//!
//! Until a callback is installed, and after it is uninstalled, recording is a
//! no-op.

#[cfg(test)]
use std::collections::HashMap;
use std::sync::Arc;

#[cfg(test)]
use parking_lot::Mutex;
use parking_lot::RwLock;

use crate::bytesize::ByteSize;

/// Receives the ads client's metrics.
pub trait Telemetry: Send + Sync {
    fn record_build_cache_error(&self, label: &str, value: String);
    fn record_client_error(&self, label: &str, value: String);
    fn record_client_operation_total(&self, label: &str);
    fn record_deserialization_error(&self, label: &str, value: String);
    fn record_http_cache_outcome(&self, label: &str, value: String);
}

// By design: telemetry is a process-wide singleton, as it will be with glean-sym.
static CALLBACK: RwLock<Option<Arc<dyn Telemetry>>> = RwLock::new(None);

/// Makes `callback` the destination of every metric recorded from now on.
pub fn install(callback: impl Telemetry + 'static) {
    // Dropping a UniFFI callback calls into foreign code, so let the replaced
    // one drop after the lock is released.
    let _replaced = CALLBACK.write().replace(Arc::new(callback));
}

/// Drops the installed callback, after which recording is a no-op.
///
/// UniFFI callbacks left hanging at shutdown crash Firefox Desktop on quit, so
/// the client uninstalls the callback when it shuts down.
pub fn uninstall() {
    // As in `install`, the removed callback drops after the lock is released.
    let _removed = CALLBACK.write().take();
}

fn with_callback(f: impl FnOnce(&dyn Telemetry)) {
    // Clone out of the lock so a slow callback never holds up `install` or
    // `uninstall`.
    let Some(callback) = CALLBACK.read().clone() else {
        return;
    };
    f(callback.as_ref());
}

/// Glean drops the end of longer string values and records an
/// `invalid_overflow` error (bug 2047640).
const MAX_STRING_SIZE: ByteSize = ByteSize::b(255);

/// Cuts `value` to fit in a Glean string, on a char boundary.
fn fit(mut value: String) -> String {
    let max = MAX_STRING_SIZE.as_u64() as usize;
    if value.len() > max {
        // `str::floor_char_boundary` does this, but needs Rust 1.91 and
        // mozilla-central still builds with 1.90.
        let mut end = max;
        while !value.is_char_boundary(end) {
            end -= 1;
        }
        value.truncate(end);
    }
    value
}

pub(super) fn build_cache_error(label: &str, value: String) {
    with_callback(|cb| cb.record_build_cache_error(label, fit(value)));
}

pub(super) fn client_error(label: &str, value: String) {
    with_callback(|cb| cb.record_client_error(label, fit(value)));
}

pub(super) fn client_operation_total(label: &str) {
    with_callback(|cb| cb.record_client_operation_total(label));
}

pub(super) fn deserialization_error(label: &str, value: String) {
    with_callback(|cb| cb.record_deserialization_error(label, fit(value)));
}

pub(super) fn http_cache_outcome(label: &str, value: String) {
    with_callback(|cb| cb.record_http_cache_outcome(label, fit(value)));
}

/// Serializes tests that install a callback, since they all share the one
/// singleton.
#[cfg(test)]
pub fn test_lock() -> parking_lot::MutexGuard<'static, ()> {
    static LOCK: parking_lot::Mutex<()> = parking_lot::Mutex::new(());
    LOCK.lock()
}

#[cfg(test)]
#[derive(Clone, Default)]
pub struct RecordingTelemetry {
    pub build_cache_errors: Arc<Mutex<HashMap<String, Vec<String>>>>,
    pub client_errors: Arc<Mutex<HashMap<String, Vec<String>>>>,
    pub client_operation_totals: Arc<Mutex<HashMap<String, u32>>>,
    pub deserialization_errors: Arc<Mutex<HashMap<String, Vec<String>>>>,
    pub http_cache_outcomes: Arc<Mutex<HashMap<String, Vec<String>>>>,
}

#[cfg(test)]
impl Telemetry for RecordingTelemetry {
    fn record_build_cache_error(&self, label: &str, value: String) {
        self.build_cache_errors
            .lock()
            .entry(label.to_string())
            .or_default()
            .push(value);
    }
    fn record_client_error(&self, label: &str, value: String) {
        self.client_errors
            .lock()
            .entry(label.to_string())
            .or_default()
            .push(value);
    }
    fn record_client_operation_total(&self, label: &str) {
        *self
            .client_operation_totals
            .lock()
            .entry(label.to_string())
            .or_default() += 1;
    }
    fn record_deserialization_error(&self, label: &str, value: String) {
        self.deserialization_errors
            .lock()
            .entry(label.to_string())
            .or_default()
            .push(value);
    }
    fn record_http_cache_outcome(&self, label: &str, value: String) {
        self.http_cache_outcomes
            .lock()
            .entry(label.to_string())
            .or_default()
            .push(value);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::telemetry::{record_client_operation, ClientOperation};

    #[test]
    fn test_fit_cuts_on_a_char_boundary() {
        assert_eq!(fit("short".into()), "short");
        assert_eq!(fit("a".repeat(300)).len() as u64, MAX_STRING_SIZE.as_u64());
        let cut = fit("é".repeat(200));
        assert_eq!(cut.len(), 254);
        assert!(cut.chars().all(|c| c == 'é'));
    }

    #[test]
    fn test_records_to_installed_callback() {
        let _lock = test_lock();
        let recording = RecordingTelemetry::default();
        install(recording.clone());

        record_client_operation(ClientOperation::ReportAd);
        // Other tests may record concurrently, so only look for ours.
        assert!(recording
            .client_operation_totals
            .lock()
            .keys()
            .any(|label| label == "report_ad"));

        uninstall();
        recording.client_operation_totals.lock().clear();
        record_client_operation(ClientOperation::ReportAd);
        assert!(recording.client_operation_totals.lock().is_empty());
    }
}
