// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#![allow(clippy::uninlined_format_args)]
#![deny(rustdoc::broken_intra_doc_links)]
#![deny(missing_docs)]

//! Glean is a modern approach for recording and sending Telemetry data.
//!
//! It's in use at Mozilla.
//!
//! All documentation can be found online:
//!
//! ## [The Glean SDK Book](https://mozilla.github.io/glean)
//!
//! ## Example
//!
//! Initialize Glean, register a ping and then send it.
//!
//! ```rust,no_run
//! # use glean::{ConfigurationBuilder, ClientInfoMetrics, Error, private::*};
//! let cfg = ConfigurationBuilder::new(true, "/tmp/data", "org.mozilla.glean_core.example").build();
//! glean::initialize(cfg, ClientInfoMetrics::unknown());
//!
//! let prototype_ping = PingType::new("prototype", true, true, true, true, true, vec!(), vec!(), true, vec![]);
//!
//! prototype_ping.submit(None);
//! ```

use std::collections::HashMap;
use std::path::Path;

use configuration::DEFAULT_GLEAN_ENDPOINT;
pub use configuration::{Builder as ConfigurationBuilder, Configuration};
pub use core_metrics::ClientInfoMetrics;
pub use glean_core::{
    metrics::{
        Datetime, DistributionData, MemoryUnit, MetricIdentifier, Rate, RecordedEvent, TimeUnit,
        TimerId,
    },
    traits, AttributionMetrics, CommonMetricData, DistributionMetrics, Error, ErrorType, Glean,
    HistogramType, LabeledMetricData, Lifetime, PingRateLimit, RecordedExperiment, Result,
};

mod configuration;
mod core_metrics;
pub mod net;
pub mod private;
mod system;

#[cfg(test)]
mod common_test;

const LANGUAGE_BINDING_NAME: &str = "Rust";

/// Creates and initializes a new Glean object.
///
/// See [`glean_core::Glean::new`] for more information.
///
/// # Arguments
///
/// * `cfg` - the [`Configuration`] options to initialize with.
/// * `client_info` - the [`ClientInfoMetrics`] values used to set Glean
///   core metrics.
pub fn initialize(cfg: Configuration, client_info: ClientInfoMetrics) {
    initialize_internal(cfg, client_info);
}

struct GleanEvents {
    /// An instance of the upload manager
    upload_manager: net::UploadManager,
}

impl glean_core::OnGleanEvents for GleanEvents {
    fn initialize_finished(&self) {
        // intentionally left empty
    }

    fn trigger_upload(&self) -> Result<(), glean_core::CallbackError> {
        self.upload_manager.trigger_upload();
        Ok(())
    }

    fn start_metrics_ping_scheduler(&self) -> bool {
        // We rely on the glean-core MPS.
        // We always trigger an upload as it might have submitted a ping.
        true
    }

    fn cancel_uploads(&self) -> Result<(), glean_core::CallbackError> {
        // intentionally left empty
        Ok(())
    }

    fn shutdown(&self) -> Result<(), glean_core::CallbackError> {
        self.upload_manager.shutdown();
        Ok(())
    }
}

fn initialize_internal(cfg: Configuration, client_info: ClientInfoMetrics) -> Option<()> {
    // Initialize the ping uploader.
    let upload_manager = net::UploadManager::new(
        cfg.server_endpoint
            .unwrap_or_else(|| DEFAULT_GLEAN_ENDPOINT.to_string()),
        cfg.uploader
            .unwrap_or_else(|| Box::new(net::HttpUploader) as Box<dyn net::PingUploader>),
    );

    // Now make this the global object available to others.
    let callbacks = Box::new(GleanEvents { upload_manager });

    let core_cfg = glean_core::InternalConfiguration {
        upload_enabled: cfg.upload_enabled,
        data_path: cfg.data_path.display().to_string(),
        application_id: cfg.application_id.clone(),
        language_binding_name: LANGUAGE_BINDING_NAME.into(),
        max_events: cfg.max_events.map(|m| m as u32),
        delay_ping_lifetime_io: cfg.delay_ping_lifetime_io,
        app_build: client_info.app_build.clone(),
        use_core_mps: cfg.use_core_mps,
        trim_data_to_registered_pings: cfg.trim_data_to_registered_pings,
        log_level: cfg.log_level,
        rate_limit: cfg.rate_limit,
        enable_event_timestamps: cfg.enable_event_timestamps,
        experimentation_id: cfg.experimentation_id,
        enable_internal_pings: cfg.enable_internal_pings,
        ping_schedule: cfg.ping_schedule,
        ping_lifetime_threshold: cfg.ping_lifetime_threshold as u64,
        ping_lifetime_max_time: cfg.ping_lifetime_max_time.as_millis() as u64,
    };

    glean_core::glean_initialize(core_cfg, client_info.into(), callbacks);
    Some(())
}

/// Shuts down Glean in an orderly fashion.
pub fn shutdown() {
    glean_core::shutdown()
}

/// **DEPRECATED** Sets whether upload is enabled or not.
///
/// **DEPRECATION NOTICE**:
/// This API is deprecated. Use `set_collection_enabled` instead.
///
/// See [`glean_core::Glean::set_upload_enabled`].
pub fn set_upload_enabled(enabled: bool) {
    glean_core::glean_set_upload_enabled(enabled)
}

/// Sets whether upload is enabled or not.
///
/// See [`glean_core::Glean::set_upload_enabled`].
pub fn set_collection_enabled(enabled: bool) {
    glean_core::glean_set_collection_enabled(enabled)
}

/// Collects and submits a ping for eventual uploading by name.
///
/// Note that this needs to be public in order for RLB consumers to
/// use Glean debugging facilities.
///
/// See [`glean_core::Glean.submit_ping_by_name`].
pub fn submit_ping_by_name(ping: &str, reason: Option<&str>) {
    let ping = ping.to_string();
    let reason = reason.map(|s| s.to_string());
    glean_core::glean_submit_ping_by_name(ping, reason)
}

/// Indicate that an experiment is running.  Glean will then add an
/// experiment annotation to the environment which is sent with pings. This
/// infomration is not persisted between runs.
///
/// See [`glean_core::Glean::set_experiment_active`].
pub fn set_experiment_active(
    experiment_id: String,
    branch: String,
    extra: Option<HashMap<String, String>>,
) {
    glean_core::glean_set_experiment_active(experiment_id, branch, extra.unwrap_or_default())
}

/// Indicate that an experiment is no longer running.
///
/// See [`glean_core::Glean::set_experiment_inactive`].
pub fn set_experiment_inactive(experiment_id: String) {
    glean_core::glean_set_experiment_inactive(experiment_id)
}

/// Dynamically set the experimentation identifier, as opposed to setting it through the configuration
/// during initialization.
pub fn set_experimentation_id(experimentation_id: String) {
    glean_core::glean_set_experimentation_id(experimentation_id);
}

/// TEST ONLY FUNCTION.
/// Gets stored experimentation id.
pub fn test_get_experimentation_id() -> Option<String> {
    glean_core::glean_test_get_experimentation_id()
}

/// Set the remote configuration values for the metrics' disabled property
///
/// See [`glean_core::Glean::glean_apply_server_knobs_config`].
pub fn glean_apply_server_knobs_config(json: String) {
    glean_core::glean_apply_server_knobs_config(json)
}

/// Performs the collection/cleanup operations required by becoming active.
///
/// This functions generates a baseline ping with reason `active`
/// and then sets the dirty bit.
/// This should be called whenever the consuming product becomes active (e.g.
/// getting to foreground).
pub fn handle_client_active() {
    glean_core::glean_handle_client_active()
}

/// Performs the collection/cleanup operations required by becoming inactive.
///
/// This functions generates a baseline and an events ping with reason
/// `inactive` and then clears the dirty bit.
/// This should be called whenever the consuming product becomes inactive (e.g.
/// getting to background).
pub fn handle_client_inactive() {
    glean_core::glean_handle_client_inactive()
}

/// TEST ONLY FUNCTION.
/// Checks if an experiment is currently active.
pub fn test_is_experiment_active(experiment_id: String) -> bool {
    glean_core::glean_test_get_experiment_data(experiment_id).is_some()
}

/// TEST ONLY FUNCTION.
/// Returns the [`RecordedExperiment`] for the given `experiment_id` or panics if
/// the id isn't found.
pub fn test_get_experiment_data(experiment_id: String) -> Option<RecordedExperiment> {
    glean_core::glean_test_get_experiment_data(experiment_id)
}

/// Destroy the global Glean state.
pub(crate) fn destroy_glean(clear_stores: bool, data_path: &Path) {
    let data_path = data_path.display().to_string();
    glean_core::glean_test_destroy_glean(clear_stores, Some(data_path))
}

/// TEST ONLY FUNCTION.
/// Resets the Glean state and triggers init again.
pub fn test_reset_glean(cfg: Configuration, client_info: ClientInfoMetrics, clear_stores: bool) {
    destroy_glean(clear_stores, &cfg.data_path);
    initialize_internal(cfg, client_info);
    glean_core::join_init();
}

/// Sets a debug view tag.
///
/// When the debug view tag is set, pings are sent with a `X-Debug-ID` header with the
/// value of the tag and are sent to the ["Ping Debug Viewer"](https://mozilla.github.io/glean/book/dev/core/internal/debug-pings.html).
///
/// # Arguments
///
/// * `tag` - A valid HTTP header value. Must match the regex: "[a-zA-Z0-9-]{1,20}".
///
/// # Returns
///
/// This will return `false` in case `tag` is not a valid tag and `true` otherwise.
/// If called before Glean is initialized it will always return `true`.
pub fn set_debug_view_tag(tag: &str) -> bool {
    glean_core::glean_set_debug_view_tag(tag.to_string())
}

/// Gets the currently set debug view tag.
///
/// The `debug_view_tag` may be set from an environment variable
/// (`GLEAN_DEBUG_VIEW_TAG`) or through the [`set_debug_view_tag`] function.
///
/// **WARNING** This function will block if Glean hasn't been initialized and
/// should only be used for debug purposes.
///
/// # Returns
///
/// Return the value for the debug view tag or [`None`] if it hasn't been set.
pub fn glean_get_debug_view_tag() -> Option<String> {
    glean_core::glean_get_debug_view_tag()
}

/// Sets the log pings debug option.
///
/// When the log pings debug option is `true`,
/// we log the payload of all succesfully assembled pings.
///
/// # Arguments
///
/// * `value` - The value of the log pings option
pub fn set_log_pings(value: bool) {
    glean_core::glean_set_log_pings(value)
}

/// Gets the current log pings value.
///
/// The `log_pings` option may be set from an environment variable (`GLEAN_LOG_PINGS`)
/// or through the [`set_log_pings`] function.
///
/// **WARNING** This function will block if Glean hasn't been initialized and
/// should only be used for debug purposes.
///
/// # Returns
///
/// Return the value for the log pings debug option.
pub fn glean_get_log_pings() -> bool {
    glean_core::glean_get_log_pings()
}

/// Sets source tags.
///
/// Overrides any existing source tags.
/// Source tags will show in the destination datasets, after ingestion.
///
/// **Note** If one or more tags are invalid, all tags are ignored.
///
/// # Arguments
///
/// * `tags` - A vector of at most 5 valid HTTP header values. Individual
///   tags must match the regex: "[a-zA-Z0-9-]{1,20}".
pub fn set_source_tags(tags: Vec<String>) {
    glean_core::glean_set_source_tags(tags);
}

/// Returns a timestamp corresponding to "now" with millisecond precision.
pub fn get_timestamp_ms() -> u64 {
    glean_core::get_timestamp_ms()
}

/// Asks the database to persist ping-lifetime data to disk.
///
/// Probably expensive to call.
/// Only has effect when Glean is configured with `delay_ping_lifetime_io: true`.
/// If Glean hasn't been initialized this will dispatch and return Ok(()),
/// otherwise it will block until the persist is done and return its Result.
pub fn persist_ping_lifetime_data() {
    glean_core::glean_persist_ping_lifetime_data();
}

/// Gets a list of currently registered ping names.
///
/// **WARNING** This function will block if Glean hasn't been initialized and
/// should only be used for debug purposes.
///
/// # Returns
///
/// The list of ping names that are currently registered.
pub fn get_registered_ping_names() -> Vec<String> {
    glean_core::glean_get_registered_ping_names()
}

/// Updates attribution fields with new values.
/// AttributionMetrics fields with `None` values will not overwrite older values.
pub fn update_attribution(attribution: AttributionMetrics) {
    glean_core::glean_update_attribution(attribution);
}

/// **TEST-ONLY Method**
///
/// Returns the current attribution metrics.
pub fn test_get_attribution() -> AttributionMetrics {
    glean_core::glean_test_get_attribution()
}

/// Updates distribution fields with new values.
/// DistributionMetrics fields with `None` values will not overwrite older values.
pub fn update_distribution(distribution: DistributionMetrics) {
    glean_core::glean_update_distribution(distribution);
}

/// **TEST-ONLY Method**
///
/// Returns the current distribution metrics.
pub fn test_get_distribution() -> DistributionMetrics {
    glean_core::glean_test_get_distribution()
}

#[cfg(test)]
mod test;
