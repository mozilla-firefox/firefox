/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::sync::Arc;

use crate::telemetry::{self, Telemetry};

/// Receives the ads client's metrics, for the app to record in Glean.
#[uniffi::export(callback_interface)]
pub trait MozAdsTelemetry: Send + Sync {
    /// The HTTP cache could not be built (`ads_client.build_cache_error`).
    fn record_build_cache_error(&self, label: String, value: String);
    /// A client operation failed (`ads_client.client_error`).
    fn record_client_error(&self, label: String, value: String);
    /// A client operation was attempted (`ads_client.client_operation_total`).
    fn record_client_operation_total(&self, label: String);
    /// An ad in a MARS response could not be parsed and was skipped
    /// (`ads_client.deserialization_error`).
    fn record_deserialization_error(&self, label: String, value: String);
    /// The outcome of a read from the HTTP cache (`ads_client.http_cache_outcome`).
    fn record_http_cache_outcome(&self, label: String, value: String);
}

/// Makes the consumer's callback the destination of every metric recorded from now on.
pub(crate) fn install(callback: Arc<dyn MozAdsTelemetry>) {
    telemetry::install(MozAdsTelemetryWrapper(callback));
}

/// Adapts the consumer's [`MozAdsTelemetry`] callback to the core [`Telemetry`] trait.
struct MozAdsTelemetryWrapper(Arc<dyn MozAdsTelemetry>);

impl Telemetry for MozAdsTelemetryWrapper {
    fn record_build_cache_error(&self, label: &str, value: String) {
        self.0.record_build_cache_error(label.to_owned(), value);
    }

    fn record_client_error(&self, label: &str, value: String) {
        self.0.record_client_error(label.to_owned(), value);
    }

    fn record_client_operation_total(&self, label: &str) {
        self.0.record_client_operation_total(label.to_owned());
    }

    fn record_deserialization_error(&self, label: &str, value: String) {
        self.0.record_deserialization_error(label.to_owned(), value);
    }

    fn record_http_cache_outcome(&self, label: &str, value: String) {
        self.0.record_http_cache_outcome(label.to_owned(), value);
    }
}
