// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#![allow(clippy::doc_overindented_list_items)]
#![allow(clippy::significant_drop_in_scrutinee)]
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

use std::borrow::Cow;
use std::collections::HashMap;
use std::fmt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use crossbeam_channel::unbounded;
use log::LevelFilter;
use once_cell::sync::{Lazy, OnceCell};
use uuid::Uuid;

use metrics::RemoteSettingsConfig;

mod common_metric_data;
mod core;
mod core_metrics;
mod coverage;
mod database;
mod debug;
mod dispatcher;
mod error;
mod error_recording;
mod event_database;
mod glean_metrics;
mod histogram;
mod internal_metrics;
mod internal_pings;
pub mod metrics;
pub mod ping;
mod scheduler;
pub mod storage;
mod system;
pub mod traits;
pub mod upload;
mod util;

#[cfg(all(not(target_os = "android"), not(target_os = "ios")))]
mod fd_logger;

pub use crate::common_metric_data::{CommonMetricData, Lifetime};
pub use crate::core::Glean;
pub use crate::core_metrics::{AttributionMetrics, ClientInfoMetrics, DistributionMetrics};
pub use crate::error::{Error, ErrorKind, Result};
pub use crate::error_recording::{test_get_num_recorded_errors, ErrorType};
pub use crate::histogram::HistogramType;
pub use crate::metrics::labeled::{
    AllowLabeled, LabeledBoolean, LabeledCounter, LabeledCustomDistribution,
    LabeledMemoryDistribution, LabeledMetric, LabeledMetricData, LabeledQuantity, LabeledString,
    LabeledTimingDistribution,
};
pub use crate::metrics::{
    BooleanMetric, CounterMetric, CustomDistributionMetric, Datetime, DatetimeMetric,
    DenominatorMetric, DistributionData, EventMetric, LocalCustomDistribution,
    LocalMemoryDistribution, LocalTimingDistribution, MemoryDistributionMetric, MemoryUnit,
    NumeratorMetric, ObjectMetric, PingType, QuantityMetric, Rate, RateMetric, RecordedEvent,
    RecordedExperiment, StringListMetric, StringMetric, TextMetric, TimeUnit, TimerId,
    TimespanMetric, TimingDistributionMetric, UrlMetric, UuidMetric,
};
pub use crate::upload::{PingRequest, PingUploadTask, UploadResult, UploadTaskAction};

const GLEAN_VERSION: &str = env!("CARGO_PKG_VERSION");
const GLEAN_SCHEMA_VERSION: u32 = 1;
const DEFAULT_MAX_EVENTS: u32 = 500;
static KNOWN_CLIENT_ID: Lazy<Uuid> =
    Lazy::new(|| Uuid::parse_str("c0ffeec0-ffee-c0ff-eec0-ffeec0ffeec0").unwrap());

// The names of the pings directories.
pub(crate) const PENDING_PINGS_DIRECTORY: &str = "pending_pings";
pub(crate) const DELETION_REQUEST_PINGS_DIRECTORY: &str = "deletion_request";

/// Set when `glean::initialize()` returns.
/// This allows to detect calls that happen before `glean::initialize()` was called.
/// Note: The initialization might still be in progress, as it runs in a separate thread.
static INITIALIZE_CALLED: AtomicBool = AtomicBool::new(false);

/// Keep track of the debug features before Glean is initialized.
static PRE_INIT_DEBUG_VIEW_TAG: Mutex<String> = Mutex::new(String::new());
static PRE_INIT_LOG_PINGS: AtomicBool = AtomicBool::new(false);
static PRE_INIT_SOURCE_TAGS: Mutex<Vec<String>> = Mutex::new(Vec::new());

/// Keep track of pings registered before Glean is initialized.
static PRE_INIT_PING_REGISTRATION: Mutex<Vec<metrics::PingType>> = Mutex::new(Vec::new());
static PRE_INIT_PING_ENABLED: Mutex<Vec<(metrics::PingType, bool)>> = Mutex::new(Vec::new());

/// Keep track of attribution and distribution supplied before Glean is initialized.
static PRE_INIT_ATTRIBUTION: Mutex<Option<AttributionMetrics>> = Mutex::new(None);
static PRE_INIT_DISTRIBUTION: Mutex<Option<DistributionMetrics>> = Mutex::new(None);

/// Global singleton of the handles of the glean.init threads.
/// For joining. For tests.
/// (Why a Vec? There might be more than one concurrent call to initialize.)
static INIT_HANDLES: Lazy<Arc<Mutex<Vec<std::thread::JoinHandle<()>>>>> =
    Lazy::new(|| Arc::new(Mutex::new(Vec::new())));

/// Configuration for Glean
#[derive(Debug, Clone)]
pub struct InternalConfiguration {
    /// Whether upload should be enabled.
    pub upload_enabled: bool,
    /// Path to a directory to store all data in.
    pub data_path: String,
    /// The application ID (will be sanitized during initialization).
    pub application_id: String,
    /// The name of the programming language used by the binding creating this instance of Glean.
    pub language_binding_name: String,
    /// The maximum number of events to store before sending a ping containing events.
    pub max_events: Option<u32>,
    /// Whether Glean should delay persistence of data from metrics with ping lifetime.
    pub delay_ping_lifetime_io: bool,
    /// The application's build identifier. If this is different from the one provided for a previous init,
    /// and use_core_mps is `true`, we will trigger a "metrics" ping.
    pub app_build: String,
    /// Whether Glean should schedule "metrics" pings.
    pub use_core_mps: bool,
    /// Whether Glean should, on init, trim its event storage to only the registered pings.
    pub trim_data_to_registered_pings: bool,
    /// The internal logging level.
    pub log_level: Option<LevelFilter>,
    /// The rate at which pings may be uploaded before they are throttled.
    pub rate_limit: Option<PingRateLimit>,
    /// Whether to add a wallclock timestamp to all events.
    pub enable_event_timestamps: bool,
    /// An experimentation identifier derived by the application to be sent with all pings, it should
    /// be noted that this has an underlying StringMetric and so should conform to the limitations that
    /// StringMetric places on length, etc.
    pub experimentation_id: Option<String>,
    /// Whether to enable internal pings. Default: true
    pub enable_internal_pings: bool,
    /// A ping schedule map.
    /// Maps a ping name to a list of pings to schedule along with it.
    /// Only used if the ping's own ping schedule list is empty.
    pub ping_schedule: HashMap<String, Vec<String>>,

    /// Write count threshold when to auto-flush. `0` disables it.
    pub ping_lifetime_threshold: u64,
    /// After what time to auto-flush. 0 disables it.
    pub ping_lifetime_max_time: u64,
}

/// How to specify the rate at which pings may be uploaded before they are throttled.
#[derive(Debug, Clone)]
pub struct PingRateLimit {
    /// Length of time in seconds of a ping uploading interval.
    pub seconds_per_interval: u64,
    /// Number of pings that may be uploaded in a ping uploading interval.
    pub pings_per_interval: u32,
}

/// Launches a new task on the global dispatch queue with a reference to the Glean singleton.
fn launch_with_glean(callback: impl FnOnce(&Glean) + Send + 'static) {
    dispatcher::launch(|| core::with_glean(callback));
}

/// Launches a new task on the global dispatch queue with a mutable reference to the
/// Glean singleton.
fn launch_with_glean_mut(callback: impl FnOnce(&mut Glean) + Send + 'static) {
    dispatcher::launch(|| core::with_glean_mut(callback));
}

/// Block on the dispatcher emptying.
///
/// This will panic if called before Glean is initialized.
fn block_on_dispatcher() {
    dispatcher::block_on_queue()
}

/// Returns a timestamp corresponding to "now" with millisecond precision.
pub fn get_timestamp_ms() -> u64 {
    const NANOS_PER_MILLI: u64 = 1_000_000;
    zeitstempel::now() / NANOS_PER_MILLI
}

/// State to keep track for the Rust Language bindings.
///
/// This is useful for setting Glean SDK-owned metrics when
/// the state of the upload is toggled.
struct State {
    /// Client info metrics set by the application.
    client_info: ClientInfoMetrics,

    callbacks: Box<dyn OnGleanEvents>,
}

/// A global singleton storing additional state for Glean.
///
/// Requires a Mutex, because in tests we can actual reset this.
static STATE: OnceCell<Mutex<State>> = OnceCell::new();

/// Get a reference to the global state object.
///
/// Panics if no global state object was set.
#[track_caller] // If this fails we're interested in the caller.
fn global_state() -> &'static Mutex<State> {
    STATE.get().unwrap()
}

/// Attempt to get a reference to the global state object.
///
/// If it hasn't been set yet, we return None.
#[track_caller] // If this fails we're interested in the caller.
fn maybe_global_state() -> Option<&'static Mutex<State>> {
    STATE.get()
}

/// Set or replace the global bindings State object.
fn setup_state(state: State) {
    // The `OnceCell` type wrapping our state is thread-safe and can only be set once.
    // Therefore even if our check for it being empty succeeds, setting it could fail if a
    // concurrent thread is quicker in setting it.
    // However this will not cause a bigger problem, as the second `set` operation will just fail.
    // We can log it and move on.
    //
    // For all wrappers this is not a problem, as the State object is intialized exactly once on
    // calling `initialize` on the global singleton and further operations check that it has been
    // initialized.
    if STATE.get().is_none() {
        if STATE.set(Mutex::new(state)).is_err() {
            log::error!(
                "Global Glean state object is initialized already. This probably happened concurrently."
            );
        }
    } else {
        // We allow overriding the global State object to support test mode.
        // In test mode the State object is fully destroyed and recreated.
        // This all happens behind a mutex and is therefore also thread-safe.
        let mut lock = STATE.get().unwrap().lock().unwrap();
        *lock = state;
    }
}

/// A global singleton that stores listener callbacks registered with Glean
/// to receive event recording notifications.
static EVENT_LISTENERS: OnceCell<Mutex<HashMap<String, Box<dyn GleanEventListener>>>> =
    OnceCell::new();

fn event_listeners() -> &'static Mutex<HashMap<String, Box<dyn GleanEventListener>>> {
    EVENT_LISTENERS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn register_event_listener(tag: String, listener: Box<dyn GleanEventListener>) {
    let mut lock = event_listeners().lock().unwrap();
    lock.insert(tag, listener);
}

fn unregister_event_listener(tag: String) {
    let mut lock = event_listeners().lock().unwrap();
    lock.remove(&tag);
}

/// An error returned from callbacks.
#[derive(Debug)]
pub enum CallbackError {
    /// An unexpected error occured.
    UnexpectedError,
}

impl fmt::Display for CallbackError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Unexpected error")
    }
}

impl std::error::Error for CallbackError {}

impl From<uniffi::UnexpectedUniFFICallbackError> for CallbackError {
    fn from(_: uniffi::UnexpectedUniFFICallbackError) -> CallbackError {
        CallbackError::UnexpectedError
    }
}

/// A callback object used to trigger actions on the foreign-language side.
///
/// A callback object is stored in glean-core for the entire lifetime of the application.
pub trait OnGleanEvents: Send {
    /// Initialization finished.
    ///
    /// The language SDK can do additional things from within the same initializer thread,
    /// e.g. starting to observe application events for foreground/background behavior.
    /// The observer then needs to call the respective client activity API.
    fn initialize_finished(&self);

    /// Trigger the uploader whenever a ping was submitted.
    ///
    /// This should not block.
    /// The uploader needs to asynchronously poll Glean for new pings to upload.
    fn trigger_upload(&self) -> Result<(), CallbackError>;

    /// Start the Metrics Ping Scheduler.
    fn start_metrics_ping_scheduler(&self) -> bool;

    /// Called when upload is disabled and uploads should be stopped
    fn cancel_uploads(&self) -> Result<(), CallbackError>;

    /// Called on shutdown, before glean-core is fully shutdown.
    ///
    /// * This MUST NOT put any new tasks on the dispatcher.
    ///   * New tasks will be ignored.
    /// * This SHOULD NOT block arbitrarily long.
    ///   * Shutdown waits for a maximum of 30 seconds.
    fn shutdown(&self) -> Result<(), CallbackError> {
        // empty by default
        Ok(())
    }
}

/// A callback handler that receives the base identifier of recorded events
/// The identifier is in the format: `<category>.<name>`
pub trait GleanEventListener: Send {
    /// Called when an event is recorded, indicating the id of the event
    fn on_event_recorded(&self, id: String);
}

/// Initializes Glean.
///
/// # Arguments
///
/// * `cfg` - the [`InternalConfiguration`] options to initialize with.
/// * `client_info` - the [`ClientInfoMetrics`] values used to set Glean
///   core metrics.
/// * `callbacks` - A callback object, stored for the entire application lifetime.
pub fn glean_initialize(
    cfg: InternalConfiguration,
    client_info: ClientInfoMetrics,
    callbacks: Box<dyn OnGleanEvents>,
) {
    initialize_inner(cfg, client_info, callbacks);
}

/// Shuts down Glean in an orderly fashion.
pub fn glean_shutdown() {
    shutdown();
}

/// Creates and initializes a new Glean object for use in a subprocess.
///
/// Importantly, this will not send any pings at startup, since that
/// sort of management should only happen in the main process.
pub fn glean_initialize_for_subprocess(cfg: InternalConfiguration) -> bool {
    let glean = match Glean::new_for_subprocess(&cfg, true) {
        Ok(glean) => glean,
        Err(err) => {
            log::error!("Failed to initialize Glean: {}", err);
            return false;
        }
    };
    if core::setup_glean(glean).is_err() {
        return false;
    }
    log::info!("Glean initialized for subprocess");
    true
}

fn initialize_inner(
    cfg: InternalConfiguration,
    client_info: ClientInfoMetrics,
    callbacks: Box<dyn OnGleanEvents>,
) {
    if was_initialize_called() {
        log::error!("Glean should not be initialized multiple times");
        return;
    }

    let init_handle = std::thread::Builder::new()
        .name("glean.init".into())
        .spawn(move || {
            let upload_enabled = cfg.upload_enabled;
            let trim_data_to_registered_pings = cfg.trim_data_to_registered_pings;

            // Set the internal logging level.
            if let Some(level) = cfg.log_level {
                log::set_max_level(level)
            }

            let glean = match Glean::new(cfg) {
                Ok(glean) => glean,
                Err(err) => {
                    log::error!("Failed to initialize Glean: {}", err);
                    return;
                }
            };
            if core::setup_glean(glean).is_err() {
                return;
            }

            log::info!("Glean initialized");

            setup_state(State {
                client_info,
                callbacks,
            });

            let mut is_first_run = false;
            let mut dirty_flag = false;
            let mut pings_submitted = false;
            core::with_glean_mut(|glean| {
                // The debug view tag might have been set before initialize,
                // get the cached value and set it.
                let debug_tag = PRE_INIT_DEBUG_VIEW_TAG.lock().unwrap();
                if !debug_tag.is_empty() {
                    glean.set_debug_view_tag(&debug_tag);
                }

                // The log pings debug option might have been set before initialize,
                // get the cached value and set it.
                let log_pigs = PRE_INIT_LOG_PINGS.load(Ordering::SeqCst);
                if log_pigs {
                    glean.set_log_pings(log_pigs);
                }

                // The source tags might have been set before initialize,
                // get the cached value and set them.
                let source_tags = PRE_INIT_SOURCE_TAGS.lock().unwrap();
                if !source_tags.is_empty() {
                    glean.set_source_tags(source_tags.to_vec());
                }

                // Get the current value of the dirty flag so we know whether to
                // send a dirty startup baseline ping below.  Immediately set it to
                // `false` so that dirty startup pings won't be sent if Glean
                // initialization does not complete successfully.
                dirty_flag = glean.is_dirty_flag_set();
                glean.set_dirty_flag(false);

                // Perform registration of pings that were attempted to be
                // registered before init.
                let pings = PRE_INIT_PING_REGISTRATION.lock().unwrap();
                for ping in pings.iter() {
                    glean.register_ping_type(ping);
                }
                let pings = PRE_INIT_PING_ENABLED.lock().unwrap();
                for (ping, enabled) in pings.iter() {
                    glean.set_ping_enabled(ping, *enabled);
                }

                // The attribution and distribution might have been set before initialize,
                // take the cached values and set them.
                if let Some(attribution) = PRE_INIT_ATTRIBUTION.lock().unwrap().take() {
                    glean.update_attribution(attribution);
                }
                if let Some(distribution) = PRE_INIT_DISTRIBUTION.lock().unwrap().take() {
                    glean.update_distribution(distribution);
                }

                // If this is the first time ever the Glean SDK runs, make sure to set
                // some initial core metrics in case we need to generate early pings.
                // The next times we start, we would have them around already.
                is_first_run = glean.is_first_run();
                if is_first_run {
                    let state = global_state().lock().unwrap();
                    initialize_core_metrics(glean, &state.client_info);
                }

                // Deal with any pending events so we can start recording new ones
                pings_submitted = glean.on_ready_to_submit_pings(trim_data_to_registered_pings);
            });

            {
                let state = global_state().lock().unwrap();
                // We need to kick off upload in these cases:
                // 1. Pings were submitted through Glean and it is ready to upload those pings;
                // 2. Upload is disabled, to upload a possible deletion-request ping.
                if pings_submitted || !upload_enabled {
                    if let Err(e) = state.callbacks.trigger_upload() {
                        log::error!("Triggering upload failed. Error: {}", e);
                    }
                }
            }

            core::with_glean(|glean| {
                // Start the MPS if its handled within Rust.
                glean.start_metrics_ping_scheduler();
            });

            // The metrics ping scheduler might _synchronously_ submit a ping
            // so that it runs before we clear application-lifetime metrics further below.
            // For that it needs access to the `Glean` object.
            // Thus we need to unlock that by leaving the context above,
            // then re-lock it afterwards.
            // That's safe because user-visible functions will be queued and thus not execute until
            // we unblock later anyway.
            {
                let state = global_state().lock().unwrap();

                // Set up information and scheduling for Glean owned pings. Ideally, the "metrics"
                // ping startup check should be performed before any other ping, since it relies
                // on being dispatched to the API context before any other metric.
                if state.callbacks.start_metrics_ping_scheduler() {
                    if let Err(e) = state.callbacks.trigger_upload() {
                        log::error!("Triggering upload failed. Error: {}", e);
                    }
                }
            }

            core::with_glean_mut(|glean| {
                let state = global_state().lock().unwrap();

                // Check if the "dirty flag" is set. That means the product was probably
                // force-closed. If that's the case, submit a 'baseline' ping with the
                // reason "dirty_startup". We only do that from the second run.
                if !is_first_run && dirty_flag {
                    // The `submit_ping_by_name_sync` function cannot be used, otherwise
                    // startup will cause a dead-lock, since that function requests a
                    // write lock on the `glean` object.
                    // Note that unwrapping below is safe: the function will return an
                    // `Ok` value for a known ping.
                    if glean.submit_ping_by_name("baseline", Some("dirty_startup")) {
                        if let Err(e) = state.callbacks.trigger_upload() {
                            log::error!("Triggering upload failed. Error: {}", e);
                        }
                    }
                }

                // From the second time we run, after all startup pings are generated,
                // make sure to clear `lifetime: application` metrics and set them again.
                // Any new value will be sent in newly generated pings after startup.
                if !is_first_run {
                    glean.clear_application_lifetime_metrics();
                    initialize_core_metrics(glean, &state.client_info);
                }
            });

            // Signal Dispatcher that init is complete
            // bug 1839433: It is important that this happens after any init tasks
            // that shutdown() depends on. At time of writing that's only setting up
            // the global Glean, but it is probably best to flush the preinit queue
            // as late as possible in the glean.init thread.
            match dispatcher::flush_init() {
                Ok(task_count) if task_count > 0 => {
                    core::with_glean(|glean| {
                        glean_metrics::error::preinit_tasks_overflow
                            .add_sync(glean, task_count as i32);
                    });
                }
                Ok(_) => {}
                Err(err) => log::error!("Unable to flush the preinit queue: {}", err),
            }

            let state = global_state().lock().unwrap();
            state.callbacks.initialize_finished();
        })
        .expect("Failed to spawn Glean's init thread");

    // For test purposes, store the glean init thread's JoinHandle.
    INIT_HANDLES.lock().unwrap().push(init_handle);

    // Mark the initialization as called: this needs to happen outside of the
    // dispatched block!
    INITIALIZE_CALLED.store(true, Ordering::SeqCst);

    // In test mode we wait for initialization to finish.
    // This needs to run after we set `INITIALIZE_CALLED`, so it's similar to normal behavior.
    if dispatcher::global::is_test_mode() {
        join_init();
    }
}

/// TEST ONLY FUNCTION
/// Waits on all the glean.init threads' join handles.
pub fn join_init() {
    let mut handles = INIT_HANDLES.lock().unwrap();
    for handle in handles.drain(..) {
        handle.join().unwrap();
    }
}

/// Call the `shutdown` callback.
///
/// This calls the shutdown in a separate thread and waits up to 30s for it to finish.
/// If not finished in that time frame it continues.
///
/// Under normal operation that is fine, as the main process will end
/// and thus the thread will get killed.
fn uploader_shutdown() {
    let timer_id = core::with_glean(|glean| glean.additional_metrics.shutdown_wait.start_sync());
    let (tx, rx) = unbounded();

    let handle = thread::Builder::new()
        .name("glean.shutdown".to_string())
        .spawn(move || {
            let state = global_state().lock().unwrap();
            if let Err(e) = state.callbacks.shutdown() {
                log::error!("Shutdown callback failed: {e:?}");
            }

            // Best-effort sending. The other side might have timed out already.
            let _ = tx.send(()).ok();
        })
        .expect("Unable to spawn thread to wait on shutdown");

    // TODO: 30 seconds? What's a good default here? Should this be configurable?
    // Reasoning:
    //   * If we shut down early we might still be processing pending pings.
    //     In this case we wait at most 3 times for 1s = 3s before we upload.
    //   * If we're rate-limited the uploader sleeps for up to 60s.
    //     Thus waiting 30s will rarely allow another upload.
    //   * We don't know how long uploads take until we get data from bug 1814592.
    let result = rx.recv_timeout(Duration::from_secs(30));

    let stop_time = time::precise_time_ns();
    core::with_glean(|glean| {
        glean
            .additional_metrics
            .shutdown_wait
            .set_stop_and_accumulate(glean, timer_id, stop_time);
    });

    if result.is_err() {
        log::warn!("Waiting for upload failed. We're shutting down.");
    } else {
        let _ = handle.join().ok();
    }
}

/// Shuts down Glean in an orderly fashion.
pub fn shutdown() {
    // Shutdown might have been called
    // 1) Before init was called
    //    * (data loss, oh well. Not enough time to do squat)
    // 2) After init was called, but before it completed
    //    * (we're willing to wait a little bit for init to complete)
    // 3) After init completed
    //    * (we can shut down immediately)

    // Case 1: "Before init was called"
    if !was_initialize_called() {
        log::warn!("Shutdown called before Glean is initialized");
        if let Err(e) = dispatcher::kill() {
            log::error!("Can't kill dispatcher thread: {:?}", e);
        }
        return;
    }

    // Case 2: "After init was called, but before it completed"
    if core::global_glean().is_none() {
        log::warn!("Shutdown called before Glean is initialized. Waiting.");
        // We can't join on the `glean.init` thread because there's no (easy) way
        // to do that with a timeout. Instead, we wait for the preinit queue to
        // empty, which is the last meaningful thing we do on that thread.

        // TODO: Make the timeout configurable?
        // We don't need the return value, as we're less interested in whether
        // this times out than we are in whether there's a Global Glean at the end.
        let _ = dispatcher::block_on_queue_timeout(Duration::from_secs(10));
    }
    // We can't shut down Glean if there's no Glean to shut down.
    if core::global_glean().is_none() {
        log::warn!("Waiting for Glean initialization timed out. Exiting.");
        if let Err(e) = dispatcher::kill() {
            log::error!("Can't kill dispatcher thread: {:?}", e);
        }
        return;
    }

    // Case 3: "After init completed"
    crate::launch_with_glean_mut(|glean| {
        glean.cancel_metrics_ping_scheduler();
        glean.set_dirty_flag(false);
    });

    // We need to wait for above task to finish,
    // but we also don't wait around forever.
    //
    // TODO: Make the timeout configurable?
    // The default hang watchdog on Firefox waits 60s,
    // Glean's `uploader_shutdown` further below waits up to 30s.
    let timer_id = core::with_glean(|glean| {
        glean
            .additional_metrics
            .shutdown_dispatcher_wait
            .start_sync()
    });
    let blocked = dispatcher::block_on_queue_timeout(Duration::from_secs(10));

    // Always record the dispatcher wait, regardless of the timeout.
    let stop_time = time::precise_time_ns();
    core::with_glean(|glean| {
        glean
            .additional_metrics
            .shutdown_dispatcher_wait
            .set_stop_and_accumulate(glean, timer_id, stop_time);
    });
    if blocked.is_err() {
        log::error!(
            "Timeout while blocking on the dispatcher. No further shutdown cleanup will happen."
        );
        return;
    }

    if let Err(e) = dispatcher::shutdown() {
        log::error!("Can't shutdown dispatcher thread: {:?}", e);
    }

    uploader_shutdown();

    // Be sure to call this _after_ draining the dispatcher
    core::with_glean(|glean| {
        if let Err(e) = glean.persist_ping_lifetime_data() {
            log::error!("Can't persist ping lifetime data: {:?}", e);
        }
    });
}

/// Asks the database to persist ping-lifetime data to disk.
///
/// Probably expensive to call.
/// Only has effect when Glean is configured with `delay_ping_lifetime_io: true`.
/// If Glean hasn't been initialized this will dispatch and return Ok(()),
/// otherwise it will block until the persist is done and return its Result.
pub fn glean_persist_ping_lifetime_data() {
    // This is async, we can't get the Error back to the caller.
    crate::launch_with_glean(|glean| {
        let _ = glean.persist_ping_lifetime_data();
    });
}

fn initialize_core_metrics(glean: &Glean, client_info: &ClientInfoMetrics) {
    core_metrics::internal_metrics::app_build.set_sync(glean, &client_info.app_build[..]);
    core_metrics::internal_metrics::app_display_version
        .set_sync(glean, &client_info.app_display_version[..]);
    core_metrics::internal_metrics::app_build_date
        .set_sync(glean, Some(client_info.app_build_date.clone()));
    if let Some(app_channel) = client_info.channel.as_ref() {
        core_metrics::internal_metrics::app_channel.set_sync(glean, app_channel);
    }

    core_metrics::internal_metrics::os_version.set_sync(glean, &client_info.os_version);
    core_metrics::internal_metrics::architecture.set_sync(glean, &client_info.architecture);

    if let Some(android_sdk_version) = client_info.android_sdk_version.as_ref() {
        core_metrics::internal_metrics::android_sdk_version.set_sync(glean, android_sdk_version);
    }
    if let Some(windows_build_number) = client_info.windows_build_number.as_ref() {
        core_metrics::internal_metrics::windows_build_number.set_sync(glean, *windows_build_number);
    }
    if let Some(device_manufacturer) = client_info.device_manufacturer.as_ref() {
        core_metrics::internal_metrics::device_manufacturer.set_sync(glean, device_manufacturer);
    }
    if let Some(device_model) = client_info.device_model.as_ref() {
        core_metrics::internal_metrics::device_model.set_sync(glean, device_model);
    }
    if let Some(locale) = client_info.locale.as_ref() {
        core_metrics::internal_metrics::locale.set_sync(glean, locale);
    }
}

/// Checks if [`initialize`] was ever called.
///
/// # Returns
///
/// `true` if it was, `false` otherwise.
fn was_initialize_called() -> bool {
    INITIALIZE_CALLED.load(Ordering::SeqCst)
}

/// Initialize the logging system based on the target platform. This ensures
/// that logging is shown when executing the Glean SDK unit tests.
#[no_mangle]
pub extern "C" fn glean_enable_logging() {
    #[cfg(target_os = "android")]
    {
        let _ = std::panic::catch_unwind(|| {
            let filter = android_logger::FilterBuilder::new()
                .filter_module("glean_ffi", log::LevelFilter::Debug)
                .filter_module("glean_core", log::LevelFilter::Debug)
                .filter_module("glean", log::LevelFilter::Debug)
                .filter_module("glean_core::ffi", log::LevelFilter::Info)
                .build();
            android_logger::init_once(
                android_logger::Config::default()
                    .with_max_level(log::LevelFilter::Debug)
                    .with_filter(filter)
                    .with_tag("libglean_ffi"),
            );
            log::trace!("Android logging should be hooked up!")
        });
    }

    // On iOS enable logging with a level filter.
    #[cfg(target_os = "ios")]
    {
        // Debug logging in debug mode.
        // (Note: `debug_assertions` is the next best thing to determine if this is a debug build)
        #[cfg(debug_assertions)]
        let level = log::LevelFilter::Debug;
        #[cfg(not(debug_assertions))]
        let level = log::LevelFilter::Info;

        let logger = oslog::OsLogger::new("org.mozilla.glean")
            .level_filter(level)
            // Filter UniFFI log messages
            .category_level_filter("glean_core::ffi", log::LevelFilter::Info);

        match logger.init() {
            Ok(_) => log::trace!("os_log should be hooked up!"),
            // Please note that this is only expected to fail during unit tests,
            // where the logger might have already been initialized by a previous
            // test. So it's fine to print with the "logger".
            Err(_) => log::warn!("os_log was already initialized"),
        };
    }

    // When specifically requested make sure logging does something on non-Android platforms as well.
    // Use the RUST_LOG environment variable to set the desired log level,
    // e.g. setting RUST_LOG=debug sets the log level to debug.
    #[cfg(all(
        not(target_os = "android"),
        not(target_os = "ios"),
        feature = "enable_env_logger"
    ))]
    {
        match env_logger::try_init() {
            Ok(_) => log::trace!("stdout logging should be hooked up!"),
            // Please note that this is only expected to fail during unit tests,
            // where the logger might have already been initialized by a previous
            // test. So it's fine to print with the "logger".
            Err(_) => log::warn!("stdout logging was already initialized"),
        };
    }
}

/// **DEPRECATED** Sets whether upload is enabled or not.
///
/// **DEPRECATION NOTICE**:
/// This API is deprecated. Use `set_collection_enabled` instead.
pub fn glean_set_upload_enabled(enabled: bool) {
    if !was_initialize_called() {
        return;
    }

    crate::launch_with_glean_mut(move |glean| {
        let state = global_state().lock().unwrap();
        let original_enabled = glean.is_upload_enabled();

        if !enabled {
            // Stop the MPS if its handled within Rust.
            glean.cancel_metrics_ping_scheduler();
            // Stop wrapper-controlled uploader.
            if let Err(e) = state.callbacks.cancel_uploads() {
                log::error!("Canceling upload failed. Error: {}", e);
            }
        }

        glean.set_upload_enabled(enabled);

        if !original_enabled && enabled {
            initialize_core_metrics(glean, &state.client_info);
        }

        if original_enabled && !enabled {
            if let Err(e) = state.callbacks.trigger_upload() {
                log::error!("Triggering upload failed. Error: {}", e);
            }
        }
    })
}

/// Sets whether collection is enabled or not.
///
/// This replaces `set_upload_enabled`.
pub fn glean_set_collection_enabled(enabled: bool) {
    glean_set_upload_enabled(enabled)
}

/// Enable or disable a ping.
///
/// Disabling a ping causes all data for that ping to be removed from storage
/// and all pending pings of that type to be deleted.
pub fn set_ping_enabled(ping: &PingType, enabled: bool) {
    let ping = ping.clone();
    if was_initialize_called() && core::global_glean().is_some() {
        crate::launch_with_glean_mut(move |glean| glean.set_ping_enabled(&ping, enabled));
    } else {
        let m = &PRE_INIT_PING_ENABLED;
        let mut lock = m.lock().unwrap();
        lock.push((ping, enabled));
    }
}

/// Register a new [`PingType`](PingType).
pub(crate) fn register_ping_type(ping: &PingType) {
    // If this happens after Glean.initialize is called (and returns),
    // we dispatch ping registration on the thread pool.
    // Registering a ping should not block the application.
    // Submission itself is also dispatched, so it will always come after the registration.
    if was_initialize_called() && core::global_glean().is_some() {
        let ping = ping.clone();
        crate::launch_with_glean_mut(move |glean| {
            glean.register_ping_type(&ping);
        })
    } else {
        // We need to keep track of pings, so they get re-registered after a reset or
        // if ping registration is attempted before Glean initializes.
        // This state is kept across Glean resets, which should only ever happen in test mode.
        // It's a set and keeping them around forever should not have much of an impact.
        let m = &PRE_INIT_PING_REGISTRATION;
        let mut lock = m.lock().unwrap();
        lock.push(ping.clone());
    }
}

/// Gets a list of currently registered ping names.
///
/// # Returns
///
/// The list of ping names that are currently registered.
pub fn glean_get_registered_ping_names() -> Vec<String> {
    block_on_dispatcher();
    core::with_glean(|glean| {
        glean
            .get_registered_ping_names()
            .iter()
            .map(|ping| ping.to_string())
            .collect()
    })
}

/// Indicate that an experiment is running.  Glean will then add an
/// experiment annotation to the environment which is sent with pings. This
/// infomration is not persisted between runs.
///
/// See [`core::Glean::set_experiment_active`].
pub fn glean_set_experiment_active(
    experiment_id: String,
    branch: String,
    extra: HashMap<String, String>,
) {
    launch_with_glean(|glean| glean.set_experiment_active(experiment_id, branch, extra))
}

/// Indicate that an experiment is no longer running.
///
/// See [`core::Glean::set_experiment_inactive`].
pub fn glean_set_experiment_inactive(experiment_id: String) {
    launch_with_glean(|glean| glean.set_experiment_inactive(experiment_id))
}

/// TEST ONLY FUNCTION.
/// Returns the [`RecordedExperiment`] for the given `experiment_id`
/// or `None` if the id isn't found.
pub fn glean_test_get_experiment_data(experiment_id: String) -> Option<RecordedExperiment> {
    block_on_dispatcher();
    core::with_glean(|glean| glean.test_get_experiment_data(experiment_id.to_owned()))
}

/// Set an experimentation identifier dynamically.
///
/// Note: it's probably a good idea to unenroll from any experiments when identifiers change.
pub fn glean_set_experimentation_id(experimentation_id: String) {
    launch_with_glean(move |glean| {
        glean
            .additional_metrics
            .experimentation_id
            .set(experimentation_id);
    });
}

/// TEST ONLY FUNCTION.
/// Gets stored experimentation id annotation.
pub fn glean_test_get_experimentation_id() -> Option<String> {
    block_on_dispatcher();
    core::with_glean(|glean| glean.test_get_experimentation_id())
}

/// Sets a remote configuration to override metrics' default enabled/disabled
/// state
///
/// See [`core::Glean::apply_server_knobs_config`].
pub fn glean_apply_server_knobs_config(json: String) {
    // An empty config means it is not set,
    // so we avoid logging an error about it.
    if json.is_empty() {
        return;
    }

    match RemoteSettingsConfig::try_from(json) {
        Ok(cfg) => launch_with_glean(|glean| {
            glean.apply_server_knobs_config(cfg);
        }),
        Err(e) => {
            log::error!("Error setting metrics feature config: {:?}", e);
        }
    }
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
pub fn glean_set_debug_view_tag(tag: String) -> bool {
    if was_initialize_called() && core::global_glean().is_some() {
        crate::launch_with_glean_mut(move |glean| {
            glean.set_debug_view_tag(&tag);
        });
        true
    } else {
        // Glean has not been initialized yet. Cache the provided tag value.
        let m = &PRE_INIT_DEBUG_VIEW_TAG;
        let mut lock = m.lock().unwrap();
        *lock = tag;
        // When setting the debug view tag before initialization,
        // we don't validate the tag, thus this function always returns true.
        true
    }
}

/// Gets the currently set debug view tag.
///
/// # Returns
///
/// Return the value for the debug view tag or [`None`] if it hasn't been set.
pub fn glean_get_debug_view_tag() -> Option<String> {
    block_on_dispatcher();
    core::with_glean(|glean| glean.debug_view_tag().map(|tag| tag.to_string()))
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
pub fn glean_set_source_tags(tags: Vec<String>) -> bool {
    if was_initialize_called() && core::global_glean().is_some() {
        crate::launch_with_glean_mut(|glean| {
            glean.set_source_tags(tags);
        });
        true
    } else {
        // Glean has not been initialized yet. Cache the provided source tags.
        let m = &PRE_INIT_SOURCE_TAGS;
        let mut lock = m.lock().unwrap();
        *lock = tags;
        // When setting the source tags before initialization,
        // we don't validate the tags, thus this function always returns true.
        true
    }
}

/// Sets the log pings debug option.
///
/// When the log pings debug option is `true`,
/// we log the payload of all succesfully assembled pings.
///
/// # Arguments
///
/// * `value` - The value of the log pings option
pub fn glean_set_log_pings(value: bool) {
    if was_initialize_called() && core::global_glean().is_some() {
        crate::launch_with_glean_mut(move |glean| {
            glean.set_log_pings(value);
        });
    } else {
        PRE_INIT_LOG_PINGS.store(value, Ordering::SeqCst);
    }
}

/// Gets the current log pings value.
///
/// # Returns
///
/// Return the value for the log pings debug option.
pub fn glean_get_log_pings() -> bool {
    block_on_dispatcher();
    core::with_glean(|glean| glean.log_pings())
}

/// Performs the collection/cleanup operations required by becoming active.
///
/// This functions generates a baseline ping with reason `active`
/// and then sets the dirty bit.
/// This should be called whenever the consuming product becomes active (e.g.
/// getting to foreground).
pub fn glean_handle_client_active() {
    dispatcher::launch(|| {
        core::with_glean_mut(|glean| {
            glean.handle_client_active();
        });

        // The above call may generate pings, so we need to trigger
        // the uploader. It's fine to trigger it if no ping was generated:
        // it will bail out.
        let state = global_state().lock().unwrap();
        if let Err(e) = state.callbacks.trigger_upload() {
            log::error!("Triggering upload failed. Error: {}", e);
        }
    });

    // The previous block of code may send a ping containing the `duration` metric,
    // in `glean.handle_client_active`. We intentionally start recording a new
    // `duration` after that happens, so that the measurement gets reported when
    // calling `handle_client_inactive`.
    core_metrics::internal_metrics::baseline_duration.start();
}

/// Performs the collection/cleanup operations required by becoming inactive.
///
/// This functions generates a baseline and an events ping with reason
/// `inactive` and then clears the dirty bit.
/// This should be called whenever the consuming product becomes inactive (e.g.
/// getting to background).
pub fn glean_handle_client_inactive() {
    // This needs to be called before the `handle_client_inactive` api: it stops
    // measuring the duration of the previous activity time, before any ping is sent
    // by the next call.
    core_metrics::internal_metrics::baseline_duration.stop();

    dispatcher::launch(|| {
        core::with_glean_mut(|glean| {
            glean.handle_client_inactive();
        });

        // The above call may generate pings, so we need to trigger
        // the uploader. It's fine to trigger it if no ping was generated:
        // it will bail out.
        let state = global_state().lock().unwrap();
        if let Err(e) = state.callbacks.trigger_upload() {
            log::error!("Triggering upload failed. Error: {}", e);
        }
    })
}

/// Collect and submit a ping for eventual upload by name.
pub fn glean_submit_ping_by_name(ping_name: String, reason: Option<String>) {
    dispatcher::launch(|| {
        let sent =
            core::with_glean(move |glean| glean.submit_ping_by_name(&ping_name, reason.as_deref()));

        if sent {
            let state = global_state().lock().unwrap();
            if let Err(e) = state.callbacks.trigger_upload() {
                log::error!("Triggering upload failed. Error: {}", e);
            }
        }
    })
}

/// Collect and submit a ping (by its name) for eventual upload, synchronously.
///
/// Note: This does not trigger the uploader. The caller is responsible to do this.
pub fn glean_submit_ping_by_name_sync(ping_name: String, reason: Option<String>) -> bool {
    if !was_initialize_called() {
        return false;
    }

    core::with_opt_glean(|glean| glean.submit_ping_by_name(&ping_name, reason.as_deref()))
        .unwrap_or(false)
}

/// EXPERIMENTAL: Register a listener object to recieve notifications of event recordings.
///
/// # Arguments
///
/// * `tag` - A string identifier used to later unregister the listener
/// * `listener` - Implements the `GleanEventListener` trait
pub fn glean_register_event_listener(tag: String, listener: Box<dyn GleanEventListener>) {
    register_event_listener(tag, listener);
}

/// Unregister an event listener from recieving notifications.
///
/// Does not panic if the listener doesn't exist.
///
/// # Arguments
///
/// * `tag` - The tag used when registering the listener to be unregistered
pub fn glean_unregister_event_listener(tag: String) {
    unregister_event_listener(tag);
}

/// **TEST-ONLY Method**
///
/// Set test mode
pub fn glean_set_test_mode(enabled: bool) {
    dispatcher::global::TESTING_MODE.store(enabled, Ordering::SeqCst);
}

/// **TEST-ONLY Method**
///
/// Destroy the underlying database.
pub fn glean_test_destroy_glean(clear_stores: bool, data_path: Option<String>) {
    if was_initialize_called() {
        // Just because initialize was called doesn't mean it's done.
        join_init();

        dispatcher::reset_dispatcher();

        // Only useful if Glean initialization finished successfully
        // and set up the storage.
        let has_storage = core::with_opt_glean(|glean| {
            // We need to flush the ping lifetime data before a full shutdown.
            glean
                .storage_opt()
                .map(|storage| storage.persist_ping_lifetime_data())
                .is_some()
        })
        .unwrap_or(false);
        if has_storage {
            uploader_shutdown();
        }

        if core::global_glean().is_some() {
            core::with_glean_mut(|glean| {
                if clear_stores {
                    glean.test_clear_all_stores()
                }
                glean.destroy_db()
            });
        }

        // Allow us to go through initialization again.
        INITIALIZE_CALLED.store(false, Ordering::SeqCst);
    } else if clear_stores {
        if let Some(data_path) = data_path {
            let _ = std::fs::remove_dir_all(data_path).ok();
        } else {
            log::warn!("Asked to clear stores before initialization, but no data path given.");
        }
    }
}

/// Get the next upload task
pub fn glean_get_upload_task() -> PingUploadTask {
    core::with_opt_glean(|glean| glean.get_upload_task()).unwrap_or_else(PingUploadTask::done)
}

/// Processes the response from an attempt to upload a ping.
pub fn glean_process_ping_upload_response(uuid: String, result: UploadResult) -> UploadTaskAction {
    core::with_glean(|glean| glean.process_ping_upload_response(&uuid, result))
}

/// **TEST-ONLY Method**
///
/// Set the dirty flag
pub fn glean_set_dirty_flag(new_value: bool) {
    core::with_glean(|glean| glean.set_dirty_flag(new_value))
}

/// Updates attribution fields with new values.
/// AttributionMetrics fields with `None` values will not overwrite older values.
pub fn glean_update_attribution(attribution: AttributionMetrics) {
    if was_initialize_called() && core::global_glean().is_some() {
        core::with_glean(|glean| glean.update_attribution(attribution));
    } else {
        PRE_INIT_ATTRIBUTION
            .lock()
            .unwrap()
            .get_or_insert(Default::default())
            .update(attribution);
    }
}

/// **TEST-ONLY Method**
///
/// Returns the current attribution metrics.
/// Panics if called before init.
pub fn glean_test_get_attribution() -> AttributionMetrics {
    join_init();
    core::with_glean(|glean| glean.test_get_attribution())
}

/// Updates distribution fields with new values.
/// DistributionMetrics fields with `None` values will not overwrite older values.
pub fn glean_update_distribution(distribution: DistributionMetrics) {
    if was_initialize_called() && core::global_glean().is_some() {
        core::with_glean(|glean| glean.update_distribution(distribution));
    } else {
        PRE_INIT_DISTRIBUTION
            .lock()
            .unwrap()
            .get_or_insert(Default::default())
            .update(distribution);
    }
}

/// **TEST-ONLY Method**
///
/// Returns the current distribution metrics.
/// Panics if called before init.
pub fn glean_test_get_distribution() -> DistributionMetrics {
    join_init();
    core::with_glean(|glean| glean.test_get_distribution())
}

#[cfg(all(not(target_os = "android"), not(target_os = "ios")))]
static FD_LOGGER: OnceCell<fd_logger::FdLogger> = OnceCell::new();

/// Initialize the logging system to send JSON messages to a file descriptor
/// (Unix) or file handle (Windows).
///
/// Not available on Android and iOS.
///
/// `fd` is a writable file descriptor (on Unix) or file handle (on Windows).
///
/// # Safety
///
/// `fd` MUST be a valid open file descriptor (Unix) or file handle (Windows).
/// This function is marked safe,
/// because we can't call unsafe functions from generated UniFFI code.
#[cfg(all(not(target_os = "android"), not(target_os = "ios")))]
pub fn glean_enable_logging_to_fd(fd: u64) {
    // SAFETY:
    // This functions is unsafe.
    // Due to UniFFI restrictions we cannot mark it as such.
    //
    // `fd` MUST be a valid open file descriptor (Unix) or file handle (Windows).
    unsafe {
        // Set up logging to a file descriptor/handle. For this usage, the
        // language binding should setup a pipe and pass in the descriptor to
        // the writing side of the pipe as the `fd` parameter. Log messages are
        // written as JSON to the file descriptor.
        let logger = FD_LOGGER.get_or_init(|| fd_logger::FdLogger::new(fd));
        // Set the level so everything goes through to the language
        // binding side where it will be filtered by the language
        // binding's logging system.
        if log::set_logger(logger).is_ok() {
            log::set_max_level(log::LevelFilter::Debug);
        }
    }
}

/// Unused function. Not used on Android or iOS.
#[cfg(any(target_os = "android", target_os = "ios"))]
pub fn glean_enable_logging_to_fd(_fd: u64) {
    // intentionally left empty
}

#[allow(missing_docs)]
// uniffi-generated code should not be checked.
#[allow(clippy::all)]
mod ffi {
    use super::*;
    uniffi::include_scaffolding!("glean");

    type CowString = Cow<'static, str>;

    uniffi::custom_type!(CowString, String, {
        remote,
        lower: |s| s.into_owned(),
        try_lift: |s| Ok(Cow::from(s))
    });

    type JsonValue = serde_json::Value;

    uniffi::custom_type!(JsonValue, String, {
        remote,
        lower: |s| serde_json::to_string(&s).unwrap(),
        try_lift: |s| Ok(serde_json::from_str(&s)?)
    });
}
pub use ffi::*;

// Split unit tests to a separate file, to reduce the file of this one.
#[cfg(test)]
#[path = "lib_unit_tests.rs"]
mod tests;
