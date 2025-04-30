// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Firefox on Glean (FOG) is the name of the layer that integrates the [Glean SDK][glean-sdk] into Firefox Desktop.
//! It is currently being designed and implemented.
//!
//! The [Glean SDK][glean-sdk] is a data collection library built by Mozilla for use in its products.
//! Like [Telemetry][telemetry], it can be used to
//! (in accordance with our [Privacy Policy][privacy-policy])
//! send anonymous usage statistics to Mozilla in order to make better decisions.
//!
//! Documentation can be found online in the [Firefox Source Docs][docs].
//!
//! [glean-sdk]: https://github.com/mozilla/glean/
//! [book-of-glean]: https://mozilla.github.io/glean/book/index.html
//! [privacy-policy]: https://www.mozilla.org/privacy/
//! [docs]: https://firefox-source-docs.mozilla.org/toolkit/components/glean/

#[cfg(target_os = "android")]
use firefox_on_glean::pings;
use firefox_on_glean::{ipc, metrics};
use nserror::{nsresult, NS_ERROR_FAILURE, NS_OK};
use nsstring::{nsACString, nsAString, nsCString};
use std::cell::UnsafeCell;
use std::fs;
use std::io::ErrorKind;
use thin_vec::ThinVec;

#[macro_use]
extern crate cstr;
#[cfg_attr(not(target_os = "android"), macro_use)]
extern crate xpcom;

mod init;

pub use init::fog_init;

use glean::{AttributionMetrics, DistributionMetrics};

#[no_mangle]
pub extern "C" fn fog_shutdown() {
    glean::shutdown();
}

#[no_mangle]
pub extern "C" fn fog_register_pings() {
    #[cfg(not(target_os = "android"))]
    log::warn!("fog_register_pings on not-Android has no effect.");

    #[cfg(target_os = "android")]
    pings::register_pings(Some("gecko"));
}

// Enough of unstable std::cell::SyncUnsafeCell for our needs, and
// compatible enough such that it can just be replaced with
// std::cell::SynUnsafeCell when it's stabilized.
#[repr(transparent)]
pub struct SyncUnsafeCell<T>(UnsafeCell<T>);

unsafe impl<T: Sync> Sync for SyncUnsafeCell<T> {}

impl<T> SyncUnsafeCell<T> {
    pub const fn new(value: T) -> Self {
        SyncUnsafeCell(UnsafeCell::new(value))
    }

    pub const fn get(&self) -> *mut T {
        self.0.get()
    }
}

static PENDING_BUF: SyncUnsafeCell<Vec<u8>> = SyncUnsafeCell::new(Vec::new());

// IPC serialization/deserialization methods
// Crucially important that the first two not be called on multiple threads.

/// # Safety
/// Only safe if only called on a single thread (the same single thread you call
/// fog_give_ipc_buf on).
#[no_mangle]
pub unsafe extern "C" fn fog_serialize_ipc_buf() -> usize {
    let pending_buf = &mut *PENDING_BUF.get();
    if let Some(buf) = ipc::take_buf() {
        *pending_buf = buf;
        pending_buf.len()
    } else {
        *pending_buf = vec![];
        0
    }
}

/// # Safety
/// Only safe if called on a single thread (the same single thread you call
/// fog_serialize_ipc_buf on), and if buf points to an allocated buffer of at
/// least buf_len bytes.
#[no_mangle]
pub unsafe extern "C" fn fog_give_ipc_buf(buf: *mut u8, buf_len: usize) -> usize {
    let pending_buf = &mut *PENDING_BUF.get();
    let pending_len = pending_buf.len();
    if buf.is_null() || buf_len < pending_len {
        return 0;
    }
    std::ptr::copy_nonoverlapping(pending_buf.as_ptr(), buf, pending_len);
    *pending_buf = Vec::new();
    pending_len
}

/// # Safety
/// Only safe if buf points to an allocated buffer of at least buf_len bytes.
/// No ownership is transfered to Rust by this method: caller owns the memory at
/// buf before and after this call.
#[no_mangle]
pub unsafe extern "C" fn fog_use_ipc_buf(buf: *const u8, buf_len: usize) {
    let slice = std::slice::from_raw_parts(buf, buf_len);
    let res = ipc::replay_from_buf(slice);
    if res.is_err() {
        log::warn!("Unable to replay ipc buffer. This will result in data loss.");
        metrics::fog_ipc::replay_failures.add(1);
    }
}

/// Sets the debug tag for pings assembled in the future.
/// Returns an error result if the provided value is not a valid tag.
#[no_mangle]
pub extern "C" fn fog_set_debug_view_tag(value: &nsACString) -> nsresult {
    let result = glean::set_debug_view_tag(&value.to_string());
    if result {
        NS_OK
    } else {
        NS_ERROR_FAILURE
    }
}

/// Submits a ping by name.
#[no_mangle]
pub extern "C" fn fog_submit_ping(ping_name: &nsACString) -> nsresult {
    let ping_name = ping_name.to_string();
    #[cfg(feature = "with_gecko")]
    firefox_on_glean::pings::record_profiler_ping_marker(&ping_name);
    glean::submit_ping_by_name(&ping_name, None);
    NS_OK
}

/// Turns ping logging on or off.
/// Returns an error if the logging failed to be configured.
#[no_mangle]
pub extern "C" fn fog_set_log_pings(value: bool) -> nsresult {
    glean::set_log_pings(value);
    NS_OK
}

/// Flushes ping-lifetime data to the db when delay_ping_lifetime_io is true.
#[no_mangle]
pub extern "C" fn fog_persist_ping_lifetime_data() -> nsresult {
    glean::persist_ping_lifetime_data();
    NS_OK
}

/// Indicate that an experiment is running.
/// Glean will add an experiment annotation which is sent with pings.
/// This information is not persisted between runs.
///
/// See [`glean_core::Glean::set_experiment_active`].
#[no_mangle]
pub extern "C" fn fog_set_experiment_active(
    experiment_id: &nsACString,
    branch: &nsACString,
    extra_keys: &ThinVec<nsCString>,
    extra_values: &ThinVec<nsCString>,
) {
    assert_eq!(
        extra_keys.len(),
        extra_values.len(),
        "Experiment extra keys and values differ in length."
    );
    let extra = if extra_keys.is_empty() {
        None
    } else {
        Some(
            extra_keys
                .iter()
                .zip(extra_values.iter())
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
        )
    };
    glean::set_experiment_active(experiment_id.to_string(), branch.to_string(), extra);
}

/// Indicate that an experiment is no longer running.
///
/// See [`glean_core::Glean::set_experiment_inactive`].
#[no_mangle]
pub extern "C" fn fog_set_experiment_inactive(experiment_id: &nsACString) {
    glean::set_experiment_inactive(experiment_id.to_string());
}

/// TEST ONLY FUNCTION
///
/// Returns true if the identified experiment is active.
#[no_mangle]
pub extern "C" fn fog_test_is_experiment_active(experiment_id: &nsACString) -> bool {
    glean::test_is_experiment_active(experiment_id.to_string())
}

/// TEST ONLY FUNCTION
///
/// Fills `branch`, `extra_keys`, and `extra_values` with the identified experiment's data.
/// Panics if the identified experiment isn't active.
#[no_mangle]
pub extern "C" fn fog_test_get_experiment_data(
    experiment_id: &nsACString,
    branch: &mut nsACString,
    extra_keys: &mut ThinVec<nsCString>,
    extra_values: &mut ThinVec<nsCString>,
) {
    let data = glean::test_get_experiment_data(experiment_id.to_string());
    if let Some(data) = data {
        branch.assign(&data.branch);
        if let Some(extra) = data.extra {
            let (data_keys, data_values): (Vec<_>, Vec<_>) = extra.iter().unzip();
            extra_keys.extend(data_keys.into_iter().map(|key| key.into()));
            extra_values.extend(data_values.into_iter().map(|value| value.into()));
        }
    }
}

/// Sets the remote feature configuration.
///
/// See [`glean_core::Glean::set_metrics_disabled_config`].
#[no_mangle]
pub extern "C" fn fog_apply_server_knobs_config(config_json: &nsACString) {
    // Normalize null and empty strings to a stringified empty map
    if config_json == "null" || config_json.is_empty() {
        glean::glean_apply_server_knobs_config("{}".to_owned());
    }
    glean::glean_apply_server_knobs_config(config_json.to_string());
}

/// Performs Glean tasks when client state changes to inactive
///
/// See [`glean_core::Glean::handle_client_inactive`].
#[no_mangle]
pub extern "C" fn fog_internal_glean_handle_client_inactive() {
    glean::handle_client_inactive();
}

/// Apply a serverknobs config from the given path.
#[no_mangle]
pub extern "C" fn fog_apply_serverknobs(serverknobs_path: &nsAString) -> bool {
    let config_json = match fs::read_to_string(serverknobs_path.to_string()) {
        Ok(c) => c,
        Err(e) if e.kind() == ErrorKind::NotFound => {
            // not logging anything if the file is missing.
            return false;
        }
        Err(e) => {
            log::error!(
                "Boo, couldn't open serverknobs file at {}, Error: {:?}",
                serverknobs_path.to_string(),
                e,
            );
            return false;
        }
    };

    log::trace!("Loaded serverknobs config. Applying.");
    glean::glean_apply_server_knobs_config(config_json);

    true
}

#[repr(C)]
pub struct FogAttributionMetrics {
    source: nsCString,
    medium: nsCString,
    campaign: nsCString,
    term: nsCString,
    content: nsCString,
}

impl FogAttributionMetrics {
    fn take(&mut self, other: AttributionMetrics) {
        if let Some(source) = other.source {
            self.source = source.into();
        }
        if let Some(medium) = other.medium {
            self.medium = medium.into();
        }
        if let Some(campaign) = other.campaign {
            self.campaign = campaign.into();
        }
        if let Some(term) = other.term {
            self.term = term.into();
        }
        if let Some(content) = other.content {
            self.content = content.into();
        }
    }
}

impl From<&FogAttributionMetrics> for AttributionMetrics {
    fn from(value: &FogAttributionMetrics) -> Self {
        let to_opt_string = |s: &nsCString| {
            if s.is_empty() {
                None
            } else {
                Some(s.to_utf8().into_owned())
            }
        };

        AttributionMetrics {
            source: to_opt_string(&value.source),
            medium: to_opt_string(&value.medium),
            campaign: to_opt_string(&value.campaign),
            term: to_opt_string(&value.term),
            content: to_opt_string(&value.content),
        }
    }
}

#[repr(C)]
pub struct FogDistributionMetrics {
    name: nsCString,
}

impl FogDistributionMetrics {
    fn take(&mut self, other: DistributionMetrics) {
        if let Some(name) = other.name {
            self.name = name.into();
        }
    }
}

impl From<&FogDistributionMetrics> for DistributionMetrics {
    fn from(value: &FogDistributionMetrics) -> Self {
        let name = if value.name.is_empty() {
            None
        } else {
            Some(value.name.to_utf8().into_owned())
        };
        DistributionMetrics { name }
    }
}
#[no_mangle]
pub extern "C" fn fog_update_attribution(attr: &FogAttributionMetrics) {
    glean::update_attribution(attr.into());
}

#[no_mangle]
pub extern "C" fn fog_test_get_attribution(value: &mut FogAttributionMetrics) {
    value.take(glean::test_get_attribution());
}

#[no_mangle]
pub extern "C" fn fog_update_distribution(dist: &FogDistributionMetrics) {
    glean::update_distribution(dist.into());
}

#[no_mangle]
pub extern "C" fn fog_test_get_distribution(value: &mut FogDistributionMetrics) {
    value.take(glean::test_get_distribution());
}
