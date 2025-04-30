// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use chrono::{DateTime, FixedOffset};
use once_cell::sync::OnceCell;

use crate::database::Database;
use crate::debug::DebugOptions;
use crate::event_database::EventDatabase;
use crate::internal_metrics::{AdditionalMetrics, CoreMetrics, DatabaseMetrics};
use crate::internal_pings::InternalPings;
use crate::metrics::{
    self, ExperimentMetric, Metric, MetricType, PingType, RecordedExperiment, RemoteSettingsConfig,
};
use crate::ping::PingMaker;
use crate::storage::{StorageManager, INTERNAL_STORAGE};
use crate::upload::{PingUploadManager, PingUploadTask, UploadResult, UploadTaskAction};
use crate::util::{local_now_with_offset, sanitize_application_id};
use crate::{
    scheduler, system, AttributionMetrics, CommonMetricData, DistributionMetrics, ErrorKind,
    InternalConfiguration, Lifetime, PingRateLimit, Result, DEFAULT_MAX_EVENTS,
    GLEAN_SCHEMA_VERSION, GLEAN_VERSION, KNOWN_CLIENT_ID,
};

static GLEAN: OnceCell<Mutex<Glean>> = OnceCell::new();

pub fn global_glean() -> Option<&'static Mutex<Glean>> {
    GLEAN.get()
}

/// Sets or replaces the global Glean object.
pub fn setup_glean(glean: Glean) -> Result<()> {
    // The `OnceCell` type wrapping our Glean is thread-safe and can only be set once.
    // Therefore even if our check for it being empty succeeds, setting it could fail if a
    // concurrent thread is quicker in setting it.
    // However this will not cause a bigger problem, as the second `set` operation will just fail.
    // We can log it and move on.
    //
    // For all wrappers this is not a problem, as the Glean object is intialized exactly once on
    // calling `initialize` on the global singleton and further operations check that it has been
    // initialized.
    if GLEAN.get().is_none() {
        if GLEAN.set(Mutex::new(glean)).is_err() {
            log::warn!(
                "Global Glean object is initialized already. This probably happened concurrently."
            )
        }
    } else {
        // We allow overriding the global Glean object to support test mode.
        // In test mode the Glean object is fully destroyed and recreated.
        // This all happens behind a mutex and is therefore also thread-safe..
        let mut lock = GLEAN.get().unwrap().lock().unwrap();
        *lock = glean;
    }
    Ok(())
}

/// Execute `f` passing the global Glean object.
///
/// Panics if the global Glean object has not been set.
pub fn with_glean<F, R>(f: F) -> R
where
    F: FnOnce(&Glean) -> R,
{
    let glean = global_glean().expect("Global Glean object not initialized");
    let lock = glean.lock().unwrap();
    f(&lock)
}

/// Execute `f` passing the global Glean object mutable.
///
/// Panics if the global Glean object has not been set.
pub fn with_glean_mut<F, R>(f: F) -> R
where
    F: FnOnce(&mut Glean) -> R,
{
    let glean = global_glean().expect("Global Glean object not initialized");
    let mut lock = glean.lock().unwrap();
    f(&mut lock)
}

/// Execute `f` passing the global Glean object if it has been set.
///
/// Returns `None` if the global Glean object has not been set.
/// Returns `Some(T)` otherwise.
pub fn with_opt_glean<F, R>(f: F) -> Option<R>
where
    F: FnOnce(&Glean) -> R,
{
    let glean = global_glean()?;
    let lock = glean.lock().unwrap();
    Some(f(&lock))
}

/// The object holding meta information about a Glean instance.
///
/// ## Example
///
/// Create a new Glean instance, register a ping, record a simple counter and then send the final
/// ping.
///
/// ```rust,no_run
/// # use glean_core::{Glean, InternalConfiguration, CommonMetricData, metrics::*};
/// let cfg = InternalConfiguration {
///     data_path: "/tmp/glean".into(),
///     application_id: "glean.sample.app".into(),
///     language_binding_name: "Rust".into(),
///     upload_enabled: true,
///     max_events: None,
///     delay_ping_lifetime_io: false,
///     app_build: "".into(),
///     use_core_mps: false,
///     trim_data_to_registered_pings: false,
///     log_level: None,
///     rate_limit: None,
///     enable_event_timestamps: true,
///     experimentation_id: None,
///     enable_internal_pings: true,
///     ping_schedule: Default::default(),
///     ping_lifetime_threshold: 1000,
///     ping_lifetime_max_time: 2000,
/// };
/// let mut glean = Glean::new(cfg).unwrap();
/// let ping = PingType::new("sample", true, false, true, true, true, vec![], vec![], true, vec![]);
/// glean.register_ping_type(&ping);
///
/// let call_counter: CounterMetric = CounterMetric::new(CommonMetricData {
///     name: "calls".into(),
///     category: "local".into(),
///     send_in_pings: vec!["sample".into()],
///     ..Default::default()
/// });
///
/// call_counter.add_sync(&glean, 1);
///
/// ping.submit_sync(&glean, None);
/// ```
///
/// ## Note
///
/// In specific language bindings, this is usually wrapped in a singleton and all metric recording goes to a single instance of this object.
/// In the Rust core, it is possible to create multiple instances, which is used in testing.
#[derive(Debug)]
pub struct Glean {
    upload_enabled: bool,
    pub(crate) data_store: Option<Database>,
    event_data_store: EventDatabase,
    pub(crate) core_metrics: CoreMetrics,
    pub(crate) additional_metrics: AdditionalMetrics,
    pub(crate) database_metrics: DatabaseMetrics,
    pub(crate) internal_pings: InternalPings,
    data_path: PathBuf,
    application_id: String,
    ping_registry: HashMap<String, PingType>,
    start_time: DateTime<FixedOffset>,
    max_events: u32,
    is_first_run: bool,
    pub(crate) upload_manager: PingUploadManager,
    debug: DebugOptions,
    pub(crate) app_build: String,
    pub(crate) schedule_metrics_pings: bool,
    pub(crate) remote_settings_epoch: AtomicU8,
    pub(crate) remote_settings_config: Arc<Mutex<RemoteSettingsConfig>>,
    pub(crate) with_timestamps: bool,
    pub(crate) ping_schedule: HashMap<String, Vec<String>>,
}

impl Glean {
    /// Creates and initializes a new Glean object for use in a subprocess.
    ///
    /// Importantly, this will not send any pings at startup, since that
    /// sort of management should only happen in the main process.
    pub fn new_for_subprocess(cfg: &InternalConfiguration, scan_directories: bool) -> Result<Self> {
        log::info!("Creating new Glean v{}", GLEAN_VERSION);

        let application_id = sanitize_application_id(&cfg.application_id);
        if application_id.is_empty() {
            return Err(ErrorKind::InvalidConfig.into());
        }

        let data_path = Path::new(&cfg.data_path);
        let event_data_store = EventDatabase::new(data_path)?;

        // Create an upload manager with rate limiting of 15 pings every 60 seconds.
        let mut upload_manager = PingUploadManager::new(&cfg.data_path, &cfg.language_binding_name);
        let rate_limit = cfg.rate_limit.as_ref().unwrap_or(&PingRateLimit {
            seconds_per_interval: 60,
            pings_per_interval: 15,
        });
        upload_manager.set_rate_limiter(
            rate_limit.seconds_per_interval,
            rate_limit.pings_per_interval,
        );

        // We only scan the pending ping directories when calling this from a subprocess,
        // when calling this from ::new we need to scan the directories after dealing with the upload state.
        if scan_directories {
            let _scanning_thread = upload_manager.scan_pending_pings_directories(false);
        }

        let start_time = local_now_with_offset();
        let mut this = Self {
            upload_enabled: cfg.upload_enabled,
            // In the subprocess, we want to avoid accessing the database entirely.
            // The easiest way to ensure that is to just not initialize it.
            data_store: None,
            event_data_store,
            core_metrics: CoreMetrics::new(),
            additional_metrics: AdditionalMetrics::new(),
            database_metrics: DatabaseMetrics::new(),
            internal_pings: InternalPings::new(cfg.enable_internal_pings),
            upload_manager,
            data_path: PathBuf::from(&cfg.data_path),
            application_id,
            ping_registry: HashMap::new(),
            start_time,
            max_events: cfg.max_events.unwrap_or(DEFAULT_MAX_EVENTS),
            is_first_run: false,
            debug: DebugOptions::new(),
            app_build: cfg.app_build.to_string(),
            // Subprocess doesn't use "metrics" pings so has no need for a scheduler.
            schedule_metrics_pings: false,
            remote_settings_epoch: AtomicU8::new(0),
            remote_settings_config: Arc::new(Mutex::new(RemoteSettingsConfig::new())),
            with_timestamps: cfg.enable_event_timestamps,
            ping_schedule: cfg.ping_schedule.clone(),
        };

        // Ensuring these pings are registered.
        let pings = this.internal_pings.clone();
        this.register_ping_type(&pings.baseline);
        this.register_ping_type(&pings.metrics);
        this.register_ping_type(&pings.events);
        this.register_ping_type(&pings.deletion_request);

        Ok(this)
    }

    /// Creates and initializes a new Glean object.
    ///
    /// This will create the necessary directories and files in
    /// [`cfg.data_path`](InternalConfiguration::data_path). This will also initialize
    /// the core metrics.
    pub fn new(cfg: InternalConfiguration) -> Result<Self> {
        let mut glean = Self::new_for_subprocess(&cfg, false)?;

        // Creating the data store creates the necessary path as well.
        // If that fails we bail out and don't initialize further.
        let data_path = Path::new(&cfg.data_path);
        let ping_lifetime_threshold = cfg.ping_lifetime_threshold as usize;
        let ping_lifetime_max_time = Duration::from_millis(cfg.ping_lifetime_max_time);
        glean.data_store = Some(Database::new(
            data_path,
            cfg.delay_ping_lifetime_io,
            ping_lifetime_threshold,
            ping_lifetime_max_time,
        )?);

        // Set experimentation identifier (if any)
        if let Some(experimentation_id) = &cfg.experimentation_id {
            glean
                .additional_metrics
                .experimentation_id
                .set_sync(&glean, experimentation_id.to_string());
        }

        // The upload enabled flag may have changed since the last run, for
        // example by the changing of a config file.
        if cfg.upload_enabled {
            // If upload is enabled, just follow the normal code path to
            // instantiate the core metrics.
            glean.on_upload_enabled();
        } else {
            // If upload is disabled, then clear the metrics
            // but do not send a deletion request ping.
            // If we have run before, and we have an old client_id,
            // do the full upload disabled operations to clear metrics
            // and send a deletion request ping.
            match glean
                .core_metrics
                .client_id
                .get_value(&glean, Some("glean_client_info"))
            {
                None => glean.clear_metrics(),
                Some(uuid) => {
                    if uuid == *KNOWN_CLIENT_ID {
                        // Previously Glean kept the KNOWN_CLIENT_ID stored.
                        // Let's ensure we erase it now.
                        if let Some(data) = glean.data_store.as_ref() {
                            _ = data.remove_single_metric(
                                Lifetime::User,
                                "glean_client_info",
                                "client_id",
                            );
                        }
                    } else {
                        // Temporarily enable uploading so we can submit a
                        // deletion request ping.
                        glean.upload_enabled = true;
                        glean.on_upload_disabled(true);
                    }
                }
            }
        }

        // We set this only for non-subprocess situations.
        // If internal pings are disabled, we don't set up the MPS either,
        // it wouldn't send any data anyway.
        glean.schedule_metrics_pings = cfg.enable_internal_pings && cfg.use_core_mps;

        // We only scan the pendings pings directories **after** dealing with the upload state.
        // If upload is disabled, we delete all pending pings files
        // and we need to do that **before** scanning the pending pings folder
        // to ensure we don't enqueue pings before their files are deleted.
        let _scanning_thread = glean.upload_manager.scan_pending_pings_directories(true);

        Ok(glean)
    }

    /// For tests make it easy to create a Glean object using only the required configuration.
    #[cfg(test)]
    pub(crate) fn with_options(
        data_path: &str,
        application_id: &str,
        upload_enabled: bool,
        enable_internal_pings: bool,
    ) -> Self {
        let cfg = InternalConfiguration {
            data_path: data_path.into(),
            application_id: application_id.into(),
            language_binding_name: "Rust".into(),
            upload_enabled,
            max_events: None,
            delay_ping_lifetime_io: false,
            app_build: "Unknown".into(),
            use_core_mps: false,
            trim_data_to_registered_pings: false,
            log_level: None,
            rate_limit: None,
            enable_event_timestamps: true,
            experimentation_id: None,
            enable_internal_pings,
            ping_schedule: Default::default(),
            ping_lifetime_threshold: 0,
            ping_lifetime_max_time: 0,
        };

        let mut glean = Self::new(cfg).unwrap();

        // Disable all upload manager policies for testing
        glean.upload_manager = PingUploadManager::no_policy(data_path);

        glean
    }

    /// Destroys the database.
    ///
    /// After this Glean needs to be reinitialized.
    pub fn destroy_db(&mut self) {
        self.data_store = None;
    }

    /// Initializes the core metrics managed by Glean's Rust core.
    fn initialize_core_metrics(&mut self) {
        let need_new_client_id = match self
            .core_metrics
            .client_id
            .get_value(self, Some("glean_client_info"))
        {
            None => true,
            Some(uuid) => uuid == *KNOWN_CLIENT_ID,
        };
        if need_new_client_id {
            self.core_metrics.client_id.generate_and_set_sync(self);
        }

        if self
            .core_metrics
            .first_run_date
            .get_value(self, "glean_client_info")
            .is_none()
        {
            self.core_metrics.first_run_date.set_sync(self, None);
            // The `first_run_date` field is generated on the very first run
            // and persisted across upload toggling. We can assume that, the only
            // time it is set, that's indeed our "first run".
            self.is_first_run = true;
        }

        self.set_application_lifetime_core_metrics();
    }

    /// Initializes the database metrics managed by Glean's Rust core.
    fn initialize_database_metrics(&mut self) {
        log::trace!("Initializing database metrics");

        if let Some(size) = self
            .data_store
            .as_ref()
            .and_then(|database| database.file_size())
        {
            log::trace!("Database file size: {}", size.get());
            self.database_metrics
                .size
                .accumulate_sync(self, size.get() as i64)
        }

        if let Some(rkv_load_state) = self
            .data_store
            .as_ref()
            .and_then(|database| database.rkv_load_state())
        {
            self.database_metrics
                .rkv_load_error
                .set_sync(self, rkv_load_state)
        }
    }

    /// Signals that the environment is ready to submit pings.
    ///
    /// Should be called when Glean is initialized to the point where it can correctly assemble pings.
    /// Usually called from the language binding after all of the core metrics have been set
    /// and the ping types have been registered.
    ///
    /// # Arguments
    ///
    /// * `trim_data_to_registered_pings` - Whether we should limit to storing data only for
    ///   data belonging to pings previously registered via `register_ping_type`.
    ///
    /// # Returns
    ///
    /// Whether the "events" ping was submitted.
    pub fn on_ready_to_submit_pings(&mut self, trim_data_to_registered_pings: bool) -> bool {
        // When upload is disabled on init we already clear out metrics.
        // However at that point not all pings are registered and so we keep that data around.
        // By the time we would be ready to submit we try again cleaning out metrics from
        // now-known pings.
        if !self.upload_enabled {
            log::debug!("on_ready_to_submit_pings. let's clear pings once again.");
            self.clear_metrics();
        }

        self.event_data_store
            .flush_pending_events_on_startup(self, trim_data_to_registered_pings)
    }

    /// Sets whether upload is enabled or not.
    ///
    /// When uploading is disabled, metrics aren't recorded at all and no
    /// data is uploaded.
    ///
    /// When disabling, all pending metrics, events and queued pings are cleared.
    ///
    /// When enabling, the core Glean metrics are recreated.
    ///
    /// If the value of this flag is not actually changed, this is a no-op.
    ///
    /// # Arguments
    ///
    /// * `flag` - When true, enable metric collection.
    ///
    /// # Returns
    ///
    /// Whether the flag was different from the current value,
    /// and actual work was done to clear or reinstate metrics.
    pub fn set_upload_enabled(&mut self, flag: bool) -> bool {
        log::info!("Upload enabled: {:?}", flag);

        if self.upload_enabled != flag {
            if flag {
                self.on_upload_enabled();
            } else {
                self.on_upload_disabled(false);
            }
            true
        } else {
            false
        }
    }

    /// Enable or disable a ping.
    ///
    /// Disabling a ping causes all data for that ping to be removed from storage
    /// and all pending pings of that type to be deleted.
    ///
    /// **Note**: Do not use directly. Call `PingType::set_enabled` instead.
    #[doc(hidden)]
    pub fn set_ping_enabled(&mut self, ping: &PingType, enabled: bool) {
        ping.store_enabled(enabled);
        if !enabled {
            if let Some(data) = self.data_store.as_ref() {
                _ = data.clear_ping_lifetime_storage(ping.name());
                _ = data.clear_lifetime_storage(Lifetime::User, ping.name());
                _ = data.clear_lifetime_storage(Lifetime::Application, ping.name());
            }
            let ping_maker = PingMaker::new();
            let disabled_pings = &[ping.name()][..];
            if let Err(err) = ping_maker.clear_pending_pings(self.get_data_path(), disabled_pings) {
                log::warn!("Error clearing pending pings: {}", err);
            }
        }
    }

    /// Determines whether upload is enabled.
    ///
    /// When upload is disabled, no data will be recorded.
    pub fn is_upload_enabled(&self) -> bool {
        self.upload_enabled
    }

    /// Check if a ping is enabled.
    ///
    /// Note that some internal "ping" names are considered to be always enabled.
    ///
    /// If a ping is not known to Glean ("unregistered") it is always considered disabled.
    /// If a ping is known, it can be enabled/disabled at any point.
    /// Only data for enabled pings is recorded.
    /// Disabled pings are never submitted.
    pub fn is_ping_enabled(&self, ping: &str) -> bool {
        // We "abuse" pings/storage names for internal data.
        const DEFAULT_ENABLED: &[&str] = &[
            "glean_client_info",
            "glean_internal_info",
            // for `experimentation_id`.
            // That should probably have gone into `glean_internal_info` instead.
            "all-pings",
        ];

        // `client_info`-like stuff is always enabled.
        if DEFAULT_ENABLED.contains(&ping) {
            return true;
        }

        let Some(ping) = self.ping_registry.get(ping) else {
            return false;
        };

        ping.enabled(self)
    }

    /// Handles the changing of state from upload disabled to enabled.
    ///
    /// Should only be called when the state actually changes.
    ///
    /// The `upload_enabled` flag is set to true and the core Glean metrics are
    /// recreated.
    fn on_upload_enabled(&mut self) {
        self.upload_enabled = true;
        self.initialize_core_metrics();
        self.initialize_database_metrics();
    }

    /// Handles the changing of state from upload enabled to disabled.
    ///
    /// Should only be called when the state actually changes.
    ///
    /// A deletion_request ping is sent, all pending metrics, events and queued
    /// pings are cleared, and the client_id is set to KNOWN_CLIENT_ID.
    /// Afterward, the upload_enabled flag is set to false.
    fn on_upload_disabled(&mut self, during_init: bool) {
        // The upload_enabled flag should be true here, or the deletion ping
        // won't be submitted.
        let reason = if during_init {
            Some("at_init")
        } else {
            Some("set_upload_enabled")
        };
        if !self
            .internal_pings
            .deletion_request
            .submit_sync(self, reason)
        {
            log::error!("Failed to submit deletion-request ping on optout.");
        }
        self.clear_metrics();
        self.upload_enabled = false;
    }

    /// Clear any pending metrics when telemetry is disabled.
    fn clear_metrics(&mut self) {
        // Clear the pending pings queue and acquire the lock
        // so that it can't be accessed until this function is done.
        let _lock = self.upload_manager.clear_ping_queue();

        // Clear any pending pings that follow `collection_enabled`.
        let ping_maker = PingMaker::new();
        let disabled_pings = self
            .ping_registry
            .iter()
            .filter(|&(_ping_name, ping)| ping.follows_collection_enabled())
            .map(|(ping_name, _ping)| &ping_name[..])
            .collect::<Vec<_>>();
        if let Err(err) = ping_maker.clear_pending_pings(self.get_data_path(), &disabled_pings) {
            log::warn!("Error clearing pending pings: {}", err);
        }

        // Delete all stored metrics.
        // Note that this also includes the ping sequence numbers, so it has
        // the effect of resetting those to their initial values.
        if let Some(data) = self.data_store.as_ref() {
            _ = data.clear_lifetime_storage(Lifetime::User, "glean_internal_info");
            _ = data.remove_single_metric(Lifetime::User, "glean_client_info", "client_id");
            for (ping_name, ping) in &self.ping_registry {
                if ping.follows_collection_enabled() {
                    _ = data.clear_ping_lifetime_storage(ping_name);
                    _ = data.clear_lifetime_storage(Lifetime::User, ping_name);
                    _ = data.clear_lifetime_storage(Lifetime::Application, ping_name);
                }
            }
        }
        if let Err(err) = self.event_data_store.clear_all() {
            log::warn!("Error clearing pending events: {}", err);
        }

        // This does not clear the experiments store (which isn't managed by the
        // StorageEngineManager), since doing so would mean we would have to have the
        // application tell us again which experiments are active if telemetry is
        // re-enabled.
    }

    /// Gets the application ID as specified on instantiation.
    pub fn get_application_id(&self) -> &str {
        &self.application_id
    }

    /// Gets the data path of this instance.
    pub fn get_data_path(&self) -> &Path {
        &self.data_path
    }

    /// Gets a handle to the database.
    #[track_caller] // If this fails we're interested in the caller.
    pub fn storage(&self) -> &Database {
        self.data_store.as_ref().expect("No database found")
    }

    /// Gets an optional handle to the database.
    pub fn storage_opt(&self) -> Option<&Database> {
        self.data_store.as_ref()
    }

    /// Gets a handle to the event database.
    pub fn event_storage(&self) -> &EventDatabase {
        &self.event_data_store
    }

    pub(crate) fn with_timestamps(&self) -> bool {
        self.with_timestamps
    }

    /// Gets the maximum number of events to store before sending a ping.
    pub fn get_max_events(&self) -> usize {
        let remote_settings_config = self.remote_settings_config.lock().unwrap();

        if let Some(max_events) = remote_settings_config.event_threshold {
            max_events as usize
        } else {
            self.max_events as usize
        }
    }

    /// Gets the next task for an uploader.
    ///
    /// This can be one of:
    ///
    /// * [`Wait`](PingUploadTask::Wait) - which means the requester should ask
    ///   again later;
    /// * [`Upload(PingRequest)`](PingUploadTask::Upload) - which means there is
    ///   a ping to upload. This wraps the actual request object;
    /// * [`Done`](PingUploadTask::Done) - which means requester should stop
    ///   asking for now.
    ///
    /// # Returns
    ///
    /// A [`PingUploadTask`] representing the next task.
    pub fn get_upload_task(&self) -> PingUploadTask {
        self.upload_manager.get_upload_task(self, self.log_pings())
    }

    /// Processes the response from an attempt to upload a ping.
    ///
    /// # Arguments
    ///
    /// * `uuid` - The UUID of the ping in question.
    /// * `status` - The upload result.
    pub fn process_ping_upload_response(
        &self,
        uuid: &str,
        status: UploadResult,
    ) -> UploadTaskAction {
        self.upload_manager
            .process_ping_upload_response(self, uuid, status)
    }

    /// Takes a snapshot for the given store and optionally clear it.
    ///
    /// # Arguments
    ///
    /// * `store_name` - The store to snapshot.
    /// * `clear_store` - Whether to clear the store after snapshotting.
    ///
    /// # Returns
    ///
    /// The snapshot in a string encoded as JSON. If the snapshot is empty, returns an empty string.
    pub fn snapshot(&mut self, store_name: &str, clear_store: bool) -> String {
        StorageManager
            .snapshot(self.storage(), store_name, clear_store)
            .unwrap_or_else(|| String::from(""))
    }

    pub(crate) fn make_path(&self, ping_name: &str, doc_id: &str) -> String {
        format!(
            "/submit/{}/{}/{}/{}",
            self.get_application_id(),
            ping_name,
            GLEAN_SCHEMA_VERSION,
            doc_id
        )
    }

    /// Collects and submits a ping by name for eventual uploading.
    ///
    /// The ping content is assembled as soon as possible, but upload is not
    /// guaranteed to happen immediately, as that depends on the upload policies.
    ///
    /// If the ping currently contains no content, it will not be sent,
    /// unless it is configured to be sent if empty.
    ///
    /// # Arguments
    ///
    /// * `ping_name` - The name of the ping to submit
    /// * `reason` - A reason code to include in the ping
    ///
    /// # Returns
    ///
    /// Whether the ping was succesfully assembled and queued.
    ///
    /// # Errors
    ///
    /// If collecting or writing the ping to disk failed.
    pub fn submit_ping_by_name(&self, ping_name: &str, reason: Option<&str>) -> bool {
        match self.get_ping_by_name(ping_name) {
            None => {
                log::error!("Attempted to submit unknown ping '{}'", ping_name);
                false
            }
            Some(ping) => ping.submit_sync(self, reason),
        }
    }

    /// Gets a [`PingType`] by name.
    ///
    /// # Returns
    ///
    /// The [`PingType`] of a ping if the given name was registered before, [`None`]
    /// otherwise.
    pub fn get_ping_by_name(&self, ping_name: &str) -> Option<&PingType> {
        self.ping_registry.get(ping_name)
    }

    /// Register a new [`PingType`](metrics/struct.PingType.html).
    pub fn register_ping_type(&mut self, ping: &PingType) {
        if self.ping_registry.contains_key(ping.name()) {
            log::debug!("Duplicate ping named '{}'", ping.name())
        }

        self.ping_registry
            .insert(ping.name().to_string(), ping.clone());
    }

    /// Gets a list of currently registered ping names.
    ///
    /// # Returns
    ///
    /// The list of ping names that are currently registered.
    pub fn get_registered_ping_names(&self) -> Vec<&str> {
        self.ping_registry.keys().map(String::as_str).collect()
    }

    /// Get create time of the Glean object.
    pub(crate) fn start_time(&self) -> DateTime<FixedOffset> {
        self.start_time
    }

    /// Indicates that an experiment is running.
    ///
    /// Glean will then add an experiment annotation to the environment
    /// which is sent with pings. This information is not persisted between runs.
    ///
    /// # Arguments
    ///
    /// * `experiment_id` - The id of the active experiment (maximum 30 bytes).
    /// * `branch` - The experiment branch (maximum 30 bytes).
    /// * `extra` - Optional metadata to output with the ping.
    pub fn set_experiment_active(
        &self,
        experiment_id: String,
        branch: String,
        extra: HashMap<String, String>,
    ) {
        let metric = ExperimentMetric::new(self, experiment_id);
        metric.set_active_sync(self, branch, extra);
    }

    /// Indicates that an experiment is no longer running.
    ///
    /// # Arguments
    ///
    /// * `experiment_id` - The id of the active experiment to deactivate (maximum 30 bytes).
    pub fn set_experiment_inactive(&self, experiment_id: String) {
        let metric = ExperimentMetric::new(self, experiment_id);
        metric.set_inactive_sync(self);
    }

    /// **Test-only API (exported for FFI purposes).**
    ///
    /// Gets stored data for the requested experiment.
    ///
    /// # Arguments
    ///
    /// * `experiment_id` - The id of the active experiment (maximum 30 bytes).
    pub fn test_get_experiment_data(&self, experiment_id: String) -> Option<RecordedExperiment> {
        let metric = ExperimentMetric::new(self, experiment_id);
        metric.test_get_value(self)
    }

    /// **Test-only API (exported for FFI purposes).**
    ///
    /// Gets stored experimentation id annotation.
    pub fn test_get_experimentation_id(&self) -> Option<String> {
        self.additional_metrics
            .experimentation_id
            .get_value(self, None)
    }

    /// Set configuration to override the default state, typically initiated from a
    /// remote_settings experiment or rollout
    ///
    /// # Arguments
    ///
    /// * `cfg` - The stringified JSON representation of a `RemoteSettingsConfig` object
    pub fn apply_server_knobs_config(&self, cfg: RemoteSettingsConfig) {
        // Set the current RemoteSettingsConfig, keeping the lock until the epoch is
        // updated to prevent against reading a "new" config but an "old" epoch
        let mut remote_settings_config = self.remote_settings_config.lock().unwrap();

        // Merge the exising metrics configuration with the supplied one
        remote_settings_config
            .metrics_enabled
            .extend(cfg.metrics_enabled);

        // Merge the exising ping configuration with the supplied one
        remote_settings_config
            .pings_enabled
            .extend(cfg.pings_enabled);

        remote_settings_config.event_threshold = cfg.event_threshold;

        // Update remote_settings epoch
        self.remote_settings_epoch.fetch_add(1, Ordering::SeqCst);
    }

    /// Persists [`Lifetime::Ping`] data that might be in memory in case
    /// [`delay_ping_lifetime_io`](InternalConfiguration::delay_ping_lifetime_io) is set
    /// or was set at a previous time.
    ///
    /// If there is no data to persist, this function does nothing.
    pub fn persist_ping_lifetime_data(&self) -> Result<()> {
        if let Some(data) = self.data_store.as_ref() {
            return data.persist_ping_lifetime_data();
        }

        Ok(())
    }

    /// Sets internally-handled application lifetime metrics.
    fn set_application_lifetime_core_metrics(&self) {
        self.core_metrics.os.set_sync(self, system::OS);
    }

    /// **This is not meant to be used directly.**
    ///
    /// Clears all the metrics that have [`Lifetime::Application`].
    pub fn clear_application_lifetime_metrics(&self) {
        log::trace!("Clearing Lifetime::Application metrics");
        if let Some(data) = self.data_store.as_ref() {
            data.clear_lifetime(Lifetime::Application);
        }

        // Set internally handled app lifetime metrics again.
        self.set_application_lifetime_core_metrics();
    }

    /// Whether or not this is the first run on this profile.
    pub fn is_first_run(&self) -> bool {
        self.is_first_run
    }

    /// Sets a debug view tag.
    ///
    /// This will return `false` in case `value` is not a valid tag.
    ///
    /// When the debug view tag is set, pings are sent with a `X-Debug-ID` header with the value of the tag
    /// and are sent to the ["Ping Debug Viewer"](https://mozilla.github.io/glean/book/dev/core/internal/debug-pings.html).
    ///
    /// # Arguments
    ///
    /// * `value` - A valid HTTP header value. Must match the regex: "[a-zA-Z0-9-]{1,20}".
    pub fn set_debug_view_tag(&mut self, value: &str) -> bool {
        self.debug.debug_view_tag.set(value.into())
    }

    /// Return the value for the debug view tag or [`None`] if it hasn't been set.
    ///
    /// The `debug_view_tag` may be set from an environment variable
    /// (`GLEAN_DEBUG_VIEW_TAG`) or through the `set_debug_view_tag` function.
    pub fn debug_view_tag(&self) -> Option<&String> {
        self.debug.debug_view_tag.get()
    }

    /// Sets source tags.
    ///
    /// This will return `false` in case `value` contains invalid tags.
    ///
    /// Ping tags will show in the destination datasets, after ingestion.
    ///
    /// **Note** If one or more tags are invalid, all tags are ignored.
    ///
    /// # Arguments
    ///
    /// * `value` - A vector of at most 5 valid HTTP header values. Individual tags must match the regex: "[a-zA-Z0-9-]{1,20}".
    pub fn set_source_tags(&mut self, value: Vec<String>) -> bool {
        self.debug.source_tags.set(value)
    }

    /// Return the value for the source tags or [`None`] if it hasn't been set.
    ///
    /// The `source_tags` may be set from an environment variable (`GLEAN_SOURCE_TAGS`)
    /// or through the [`set_source_tags`] function.
    pub(crate) fn source_tags(&self) -> Option<&Vec<String>> {
        self.debug.source_tags.get()
    }

    /// Sets the log pings debug option.
    ///
    /// This will return `false` in case we are unable to set the option.
    ///
    /// When the log pings debug option is `true`,
    /// we log the payload of all succesfully assembled pings.
    ///
    /// # Arguments
    ///
    /// * `value` - The value of the log pings option
    pub fn set_log_pings(&mut self, value: bool) -> bool {
        self.debug.log_pings.set(value)
    }

    /// Return the value for the log pings debug option or `false` if it hasn't been set.
    ///
    /// The `log_pings` option may be set from an environment variable (`GLEAN_LOG_PINGS`)
    /// or through the `set_log_pings` function.
    pub fn log_pings(&self) -> bool {
        self.debug.log_pings.get().copied().unwrap_or(false)
    }

    fn get_dirty_bit_metric(&self) -> metrics::BooleanMetric {
        metrics::BooleanMetric::new(CommonMetricData {
            name: "dirtybit".into(),
            // We don't need a category, the name is already unique
            category: "".into(),
            send_in_pings: vec![INTERNAL_STORAGE.into()],
            lifetime: Lifetime::User,
            ..Default::default()
        })
    }

    /// **This is not meant to be used directly.**
    ///
    /// Sets the value of a "dirty flag" in the permanent storage.
    ///
    /// The "dirty flag" is meant to have the following behaviour, implemented
    /// by the consumers of the FFI layer:
    ///
    /// - on mobile: set to `false` when going to background or shutting down,
    ///   set to `true` at startup and when going to foreground.
    /// - on non-mobile platforms: set to `true` at startup and `false` at
    ///   shutdown.
    ///
    /// At startup, before setting its new value, if the "dirty flag" value is
    /// `true`, then Glean knows it did not exit cleanly and can implement
    /// coping mechanisms (e.g. sending a `baseline` ping).
    pub fn set_dirty_flag(&self, new_value: bool) {
        self.get_dirty_bit_metric().set_sync(self, new_value);
    }

    /// **This is not meant to be used directly.**
    ///
    /// Checks the stored value of the "dirty flag".
    pub fn is_dirty_flag_set(&self) -> bool {
        let dirty_bit_metric = self.get_dirty_bit_metric();
        match StorageManager.snapshot_metric(
            self.storage(),
            INTERNAL_STORAGE,
            &dirty_bit_metric.meta().identifier(self),
            dirty_bit_metric.meta().inner.lifetime,
        ) {
            Some(Metric::Boolean(b)) => b,
            _ => false,
        }
    }

    /// Performs the collection/cleanup operations required by becoming active.
    ///
    /// This functions generates a baseline ping with reason `active`
    /// and then sets the dirty bit.
    pub fn handle_client_active(&mut self) {
        if !self
            .internal_pings
            .baseline
            .submit_sync(self, Some("active"))
        {
            log::info!("baseline ping not submitted on active");
        }

        self.set_dirty_flag(true);
    }

    /// Performs the collection/cleanup operations required by becoming inactive.
    ///
    /// This functions generates a baseline and an events ping with reason
    /// `inactive` and then clears the dirty bit.
    pub fn handle_client_inactive(&mut self) {
        if !self
            .internal_pings
            .baseline
            .submit_sync(self, Some("inactive"))
        {
            log::info!("baseline ping not submitted on inactive");
        }

        if !self
            .internal_pings
            .events
            .submit_sync(self, Some("inactive"))
        {
            log::info!("events ping not submitted on inactive");
        }

        self.set_dirty_flag(false);
    }

    /// **Test-only API (exported for FFI purposes).**
    ///
    /// Deletes all stored metrics.
    ///
    /// Note that this also includes the ping sequence numbers, so it has
    /// the effect of resetting those to their initial values.
    pub fn test_clear_all_stores(&self) {
        if let Some(data) = self.data_store.as_ref() {
            data.clear_all()
        }
        // We don't care about this failing, maybe the data does just not exist.
        let _ = self.event_data_store.clear_all();
    }

    /// Instructs the Metrics Ping Scheduler's thread to exit cleanly.
    /// If Glean was configured with `use_core_mps: false`, this has no effect.
    pub fn cancel_metrics_ping_scheduler(&self) {
        if self.schedule_metrics_pings {
            scheduler::cancel();
        }
    }

    /// Instructs the Metrics Ping Scheduler to being scheduling metrics pings.
    /// If Glean wsa configured with `use_core_mps: false`, this has no effect.
    pub fn start_metrics_ping_scheduler(&self) {
        if self.schedule_metrics_pings {
            scheduler::schedule(self);
        }
    }

    /// Updates attribution fields with new values.
    /// AttributionMetrics fields with `None` values will not overwrite older values.
    pub fn update_attribution(&self, attribution: AttributionMetrics) {
        if let Some(source) = attribution.source {
            self.core_metrics.attribution_source.set_sync(self, source);
        }
        if let Some(medium) = attribution.medium {
            self.core_metrics.attribution_medium.set_sync(self, medium);
        }
        if let Some(campaign) = attribution.campaign {
            self.core_metrics
                .attribution_campaign
                .set_sync(self, campaign);
        }
        if let Some(term) = attribution.term {
            self.core_metrics.attribution_term.set_sync(self, term);
        }
        if let Some(content) = attribution.content {
            self.core_metrics
                .attribution_content
                .set_sync(self, content);
        }
    }

    /// **TEST-ONLY Method**
    ///
    /// Returns the current attribution metrics.
    pub fn test_get_attribution(&self) -> AttributionMetrics {
        AttributionMetrics {
            source: self
                .core_metrics
                .attribution_source
                .get_value(self, Some("glean_client_info")),
            medium: self
                .core_metrics
                .attribution_medium
                .get_value(self, Some("glean_client_info")),
            campaign: self
                .core_metrics
                .attribution_campaign
                .get_value(self, Some("glean_client_info")),
            term: self
                .core_metrics
                .attribution_term
                .get_value(self, Some("glean_client_info")),
            content: self
                .core_metrics
                .attribution_content
                .get_value(self, Some("glean_client_info")),
        }
    }

    /// Updates distribution fields with new values.
    /// DistributionMetrics fields with `None` values will not overwrite older values.
    pub fn update_distribution(&self, distribution: DistributionMetrics) {
        if let Some(name) = distribution.name {
            self.core_metrics.distribution_name.set_sync(self, name);
        }
    }

    /// **TEST-ONLY Method**
    ///
    /// Returns the current distribution metrics.
    pub fn test_get_distribution(&self) -> DistributionMetrics {
        DistributionMetrics {
            name: self
                .core_metrics
                .distribution_name
                .get_value(self, Some("glean_client_info")),
        }
    }
}
