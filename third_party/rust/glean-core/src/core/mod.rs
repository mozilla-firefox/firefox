// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::HashMap;
use std::fs::{self, File};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use chrono::{DateTime, FixedOffset, SecondsFormat, Utc};
use malloc_size_of_derive::MallocSizeOf;
use once_cell::sync::OnceCell;
use uuid::Uuid;

#[cfg(feature = "sqlite")]
use crate::database::sqlite::MigrationResult;
use crate::database::Database;
use crate::debug::DebugOptions;
use crate::error::ClientIdFileError;
use crate::event_database::EventDatabase;
use crate::internal_metrics::{
    AdditionalMetrics, CoreMetrics, DatabaseMetrics, ExceptionState, HealthMetrics,
};
use crate::internal_pings::InternalPings;
use crate::metrics::{
    self, ExperimentMetric, Metric, MetricType, PingType, RecordedExperiment, RemoteSettingsConfig,
};
use crate::ping::PingMaker;
use crate::session::{self, EventSessionContext, SessionManager, SessionMode, SessionState};
use crate::storage::{StorageManager, INTERNAL_STORAGE};
use crate::upload::{PingUploadManager, PingUploadTask, UploadResult, UploadTaskAction};
#[cfg(feature = "sqlite")]
use crate::util::truncate_string_at_boundary;
use crate::util::{local_now_with_offset, sanitize_application_id};
use crate::{
    scheduler, system, AttributionMetrics, CommonMetricData, DistributionMetrics, ErrorKind,
    InternalConfiguration, Lifetime, PingRateLimit, Result, DEFAULT_MAX_EVENTS,
    GLEAN_SCHEMA_VERSION, GLEAN_VERSION, KNOWN_CLIENT_ID,
};

const CLIENT_ID_PLAIN_FILENAME: &str = "client_id.txt";
static GLEAN: OnceCell<Mutex<Glean>> = OnceCell::new();

/// Rate limiting defaults
/// 15 pings every 60 seconds.
pub const DEFAULT_SECONDS_PER_INTERVAL: u64 = 60;
pub const DEFAULT_PINGS_PER_INTERVAL: u32 = 15;

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
///     max_pending_pings_count: None,
///     max_pending_pings_directory_size: None,
///     session_mode: glean_core::SessionMode::Auto,
///     session_sample_rate: 1.0,
///     session_inactivity_timeout_ms: 1_800_000,
///     events_ping_acceleration_factor: None,
///     enable_store_submitted_pings: false,
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
#[derive(Debug, MallocSizeOf)]
pub struct Glean {
    upload_enabled: bool,
    pub(crate) data_store: Option<Database>,
    event_data_store: EventDatabase,
    pub(crate) core_metrics: CoreMetrics,
    pub(crate) additional_metrics: AdditionalMetrics,
    pub(crate) database_metrics: DatabaseMetrics,
    pub(crate) health_metrics: HealthMetrics,
    pub(crate) internal_pings: InternalPings,
    data_path: PathBuf,
    application_id: String,
    ping_registry: HashMap<String, PingType>,
    #[ignore_malloc_size_of = "external non-allocating type"]
    start_time: DateTime<FixedOffset>,
    max_events: u32,
    is_first_run: bool,
    pub(crate) upload_manager: PingUploadManager,
    debug: DebugOptions,
    pub(crate) app_build: String,
    pub(crate) schedule_metrics_pings: bool,
    pub(crate) remote_settings_epoch: AtomicU8,
    #[ignore_malloc_size_of = "TODO: Expose Glean's inner memory allocations (bug 1960592)"]
    pub(crate) remote_settings_config: Arc<Mutex<RemoteSettingsConfig>>,
    pub(crate) with_timestamps: bool,
    pub(crate) ping_schedule: HashMap<String, Vec<String>>,
    #[ignore_malloc_size_of = "TODO: Expose session memory allocations (bug 2043355)"]
    pub(crate) session_manager: SessionManager,
    events_ping_acceleration_factor: Option<usize>,
    pub(crate) store_submitted_pings_enabled: bool,
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
            seconds_per_interval: DEFAULT_SECONDS_PER_INTERVAL,
            pings_per_interval: DEFAULT_PINGS_PER_INTERVAL,
        });
        upload_manager.set_rate_limiter(
            rate_limit.seconds_per_interval,
            rate_limit.pings_per_interval,
        );
        if let Some(n) = cfg.max_pending_pings_count {
            upload_manager.set_max_pending_pings_count(n);
        }
        if let Some(n) = cfg.max_pending_pings_directory_size {
            upload_manager.set_max_pending_pings_directory_size(n);
        }

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
            health_metrics: HealthMetrics::new(),
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
            // The SessionManager is deliberately left in its default (hollow)
            // state for subprocesses. `restore_session_state_from_storage()`
            // is only called in `Glean::new()`, not here, so the subprocess
            // never loads or mutates the main process's persisted session
            // state. This prevents subprocesses from interfering with the
            // main process's session lifecycle (seq counters, dirty flags,
            // boundary events, etc.).
            session_manager: SessionManager::new(
                cfg.session_mode,
                cfg.session_sample_rate,
                std::time::Duration::from_millis(cfg.session_inactivity_timeout_ms),
            ),
            events_ping_acceleration_factor: cfg
                .events_ping_acceleration_factor
                .map(|x| x as usize),
            store_submitted_pings_enabled: cfg.enable_store_submitted_pings,
        };

        // Ensuring these pings are registered.
        let pings = this.internal_pings.clone();
        this.register_ping_type(&pings.baseline);
        this.register_ping_type(&pings.metrics);
        this.register_ping_type(&pings.events);
        this.register_ping_type(&pings.health);
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

        #[cfg(feature = "sqlite")]
        if let Some(state) = glean.data_store.as_mut().unwrap().migration_state.take() {
            glean
                .database_metrics
                .migrated_metrics
                .add_sync(&glean, state.migrated_metrics);
            glean
                .database_metrics
                .metrics_in_sqlite
                .add_sync(&glean, state.metrics_in_sql);
            glean
                .database_metrics
                .failed_metrics
                .add_sync(&glean, state.failed_metrics);

            let duration_ns = state.duration.as_nanos().try_into().unwrap_or(u64::MAX);
            glean
                .database_metrics
                .migration_duration
                .accumulate_raw_samples_nanos_sync(&glean, &[duration_ns]);
        }

        #[cfg(feature = "sqlite")]
        if glean.data_store.as_mut().unwrap().migration_error == MigrationResult::Error {
            glean.database_metrics.migration_error.add_sync(&glean, 1);
        }

        glean.restore_session_state_from_storage();

        // This code references different states from the "Client ID recovery" flowchart.
        // See https://mozilla.github.io/glean/dev/core/internal/client_id_recovery.html for details.

        // We don't have the database yet when we first encounter the error,
        // so we store it and apply it later.
        // state (a)
        let stored_client_id = match glean.client_id_from_file() {
            Ok(id) if id == *KNOWN_CLIENT_ID => {
                glean
                    .health_metrics
                    .file_read_error
                    .get("c0ffee-in-file")
                    .add_sync(&glean, 1);
                None
            }
            Ok(id) => Some(id),
            Err(ClientIdFileError::NotFound) => {
                // That's ok, the file might just not exist yet.
                glean
                    .health_metrics
                    .file_read_error
                    .get("file-not-found")
                    .add_sync(&glean, 1);
                None
            }
            Err(ClientIdFileError::PermissionDenied) => {
                // state (b)
                // Uhm ... who removed our permission?
                glean
                    .health_metrics
                    .file_read_error
                    .get("permission-denied")
                    .add_sync(&glean, 1);
                None
            }
            Err(ClientIdFileError::ParseError(e)) => {
                // state (b)
                log::trace!("reading cliend_id.txt. Could not parse into UUID: {e}");
                glean
                    .health_metrics
                    .file_read_error
                    .get("parse")
                    .add_sync(&glean, 1);
                None
            }
            Err(ClientIdFileError::IoError(e)) => {
                // state (b)
                // We can't handle other IO errors (most couldn't occur on this operation anyway)
                log::trace!("reading client_id.txt. Unexpected io error: {e}");
                glean
                    .health_metrics
                    .file_read_error
                    .get("io")
                    .add_sync(&glean, 1);
                None
            }
        };

        {
            let data_store = glean.data_store.as_ref().unwrap();
            let file_size = data_store.file_size().map(|n| n.get()).unwrap_or(0);

            // If we have a client ID on disk, we check the database
            if let Some(stored_client_id) = stored_client_id {
                // state (c)
                if file_size == 0 {
                    log::trace!("no database. database size={file_size}. stored_client_id={stored_client_id}");
                    // state (d)
                    glean
                        .health_metrics
                        .recovered_client_id
                        .set_from_uuid_sync(&glean, stored_client_id);
                    glean
                        .health_metrics
                        .exception_state
                        .set_sync(&glean, ExceptionState::EmptyDb);

                    // state (e) -- mitigation: store recovered client ID in DB
                    glean
                        .core_metrics
                        .client_id
                        .set_from_uuid_sync(&glean, stored_client_id);
                } else {
                    let db_client_id = glean
                        .core_metrics
                        .client_id
                        .get_value(&glean, Some("glean_client_info"));

                    match db_client_id {
                        None => {
                            // state (f)
                            log::trace!("no client_id in DB. stored_client_id={stored_client_id}");
                            glean
                                .health_metrics
                                .exception_state
                                .set_sync(&glean, ExceptionState::RegenDb);

                            // state (e) -- mitigation: store recovered client ID in DB
                            glean
                                .core_metrics
                                .client_id
                                .set_from_uuid_sync(&glean, stored_client_id);
                        }
                        Some(db_client_id) if db_client_id == *KNOWN_CLIENT_ID => {
                            // state (i)
                            log::trace!(
                                "c0ffee client_id in DB, stored_client_id={stored_client_id}"
                            );
                            glean
                                .health_metrics
                                .recovered_client_id
                                .set_from_uuid_sync(&glean, stored_client_id);
                            glean
                                .health_metrics
                                .exception_state
                                .set_sync(&glean, ExceptionState::C0ffeeInDb);

                            // If we have a recovered client ID we also overwrite the database.
                            // state (e)
                            glean
                                .core_metrics
                                .client_id
                                .set_from_uuid_sync(&glean, stored_client_id);
                        }
                        Some(db_client_id) if db_client_id == stored_client_id => {
                            // all valid. nothing to do
                            log::trace!("database consistent. db_client_id == stored_client_id: {db_client_id}");
                        }
                        Some(db_client_id) => {
                            // state (g)
                            log::trace!(
                                "client_id mismatch. db_client_id{db_client_id}, stored_client_id={stored_client_id}. Overwriting file with db's client_id."
                            );
                            glean
                                .health_metrics
                                .recovered_client_id
                                .set_from_uuid_sync(&glean, stored_client_id);
                            glean
                                .health_metrics
                                .exception_state
                                .set_sync(&glean, ExceptionState::ClientIdMismatch);

                            // state (h)
                            glean.store_client_id_with_reporting(
                                db_client_id,
                                "client_id mismatch will re-occur.",
                            );
                        }
                    }
                }
            } else {
                log::trace!("No stored client ID. Database might have it.");

                let db_client_id = glean
                    .core_metrics
                    .client_id
                    .get_value(&glean, Some("glean_client_info"));
                if let Some(db_client_id) = db_client_id {
                    // state (h)
                    glean.store_client_id_with_reporting(
                        db_client_id,
                        "Might happen on next init then.",
                    );
                } else {
                    log::trace!("Database has no client ID either. We might be fresh!");
                }
            }
        }

        // Set experimentation identifier (if any)
        if let Some(experimentation_id) = &cfg.experimentation_id {
            glean
                .additional_metrics
                .experimentation_id
                .set_sync(&glean, experimentation_id.to_string());
        }

        glean.set_early_metrics();

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
                    if let Err(e) = glean.remove_stored_client_id() {
                        log::error!("Couldn't remove client ID on disk. This might lead to a resurrection of this client ID later. Error: {e}");
                    }
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
            max_pending_pings_count: None,
            max_pending_pings_directory_size: None,
            session_mode: SessionMode::Auto,
            session_sample_rate: 1.0,
            session_inactivity_timeout_ms: 1_800_000,
            events_ping_acceleration_factor: None,
            enable_store_submitted_pings: false,
        };

        let mut glean = Self::new(cfg).unwrap();

        // Disable all upload manager policies for testing
        glean.upload_manager = PingUploadManager::no_policy(data_path);

        glean
    }

    /// Close the database connection.
    ///
    /// After this Glean needs to be reinitialized.
    pub fn close_db(&mut self) {
        self.data_store = None;
    }

    fn client_id_file_path(&self) -> PathBuf {
        self.data_path.join(CLIENT_ID_PLAIN_FILENAME)
    }

    /// Write the client ID to a separate plain file on disk
    ///
    /// Use `store_client_id_with_reporting` to handle the error cases.
    fn store_client_id(&self, client_id: Uuid) -> Result<(), ClientIdFileError> {
        let mut fp = File::create(self.client_id_file_path())?;

        let mut buffer = Uuid::encode_buffer();
        let uuid_str = client_id.hyphenated().encode_lower(&mut buffer);
        fp.write_all(uuid_str.as_bytes())?;
        fp.sync_all()?;

        Ok(())
    }

    /// Write the client ID to a separate plain file on disk
    ///
    /// When an error occurs an error message is logged and the error is counted in a metric.
    fn store_client_id_with_reporting(&self, client_id: Uuid, msg: &str) {
        if let Err(err) = self.store_client_id(client_id) {
            log::error!(
                "Could not write {client_id} to state file. {} Error: {err}",
                msg
            );
            match err {
                ClientIdFileError::NotFound => {
                    self.health_metrics
                        .file_write_error
                        .get("not-found")
                        .add_sync(self, 1);
                }
                ClientIdFileError::PermissionDenied => {
                    self.health_metrics
                        .file_write_error
                        .get("permission-denied")
                        .add_sync(self, 1);
                }
                ClientIdFileError::IoError(..) => {
                    self.health_metrics
                        .file_write_error
                        .get("io")
                        .add_sync(self, 1);
                }
                ClientIdFileError::ParseError(..) => {
                    log::error!("Parse error encountered on file write. This is impossible.");
                }
            }
        }
    }

    /// Try to load a client ID from the plain file on disk.
    fn client_id_from_file(&self) -> Result<Uuid, ClientIdFileError> {
        let uuid_str = fs::read_to_string(self.client_id_file_path())?;
        // We don't write a newline, but we still trim it. Who knows who else touches that file by accident.
        // We're also a bit more lenient in what we accept here:
        // uppercase, lowercase, with or without dashes, urn, braced (and whatever else `Uuid`
        // parses by default).
        let uuid = Uuid::try_parse(uuid_str.trim_end())?;
        Ok(uuid)
    }

    /// Remove the stored client ID from disk.
    /// Should only be called when the client ID is also removed from the database.
    fn remove_stored_client_id(&self) -> Result<(), ClientIdFileError> {
        match fs::remove_file(self.client_id_file_path()) {
            Ok(()) => Ok(()),
            Err(e) if e.kind() == io::ErrorKind::NotFound => {
                // File was already missing. No need to report that.
                Ok(())
            }
            Err(e) => Err(e.into()),
        }
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
            let new_clientid = self.core_metrics.client_id.generate_and_set_sync(self);
            self.store_client_id_with_reporting(new_clientid, "New client in database only.");
        }
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

        #[cfg(feature = "sqlite")]
        if let Some(load_state) = self
            .data_store
            .as_ref()
            .and_then(|database| database.load_state())
        {
            use crate::metrics::string::MAX_LENGTH_VALUE;
            let load_state = truncate_string_at_boundary(load_state, MAX_LENGTH_VALUE);
            self.database_metrics.load_error.set_sync(self, load_state)
        }

        #[cfg(not(feature = "sqlite"))]
        if let Some(rkv_load_state) = self
            .data_store
            .as_ref()
            .and_then(|database| database.rkv_load_state())
        {
            self.database_metrics
                .rkv_load_error
                .set_sync(self, rkv_load_state);
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

    /// Sets whether storing submitted pings is enabled or not.
    ///
    /// # Arguments
    ///
    /// * `enabled` - When true, enables storing submitted pings.
    ///
    pub fn set_store_submitted_pings_enabled(&mut self, enabled: bool) {
        self.store_submitted_pings_enabled = enabled;
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
            if let Err(err) = self.event_storage().clear_for_ping(ping.name()) {
                log::warn!("Error clearing event storage for ping: {}", err);
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
            log::trace!("Unknown ping {ping}. Assuming disabled.");
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

        if let Err(e) = self.remove_stored_client_id() {
            log::error!("Couldn't remove client ID on disk. This might lead to a resurrection of this client ID later. Error: {e}");
        }

        // Delete all stored metrics.
        // Note that this also includes the ping sequence numbers, so it has
        // the effect of resetting those to their initial values.
        if let Some(data) = self.data_store.as_ref() {
            let warn_on_error = |result, msg| {
                if let Err(e) = result {
                    log::warn!("{msg}: {e}");
                }
            };

            warn_on_error(
                data.clear_lifetime_storage(Lifetime::User, INTERNAL_STORAGE),
                "failed to clear internal storage",
            );
            warn_on_error(
                data.remove_single_metric(Lifetime::User, "glean_client_info", "client_id"),
                "failed to clear internal client info storage",
            );
            for (ping_name, ping) in &self.ping_registry {
                if ping.follows_collection_enabled() {
                    warn_on_error(
                        data.clear_ping_lifetime_storage(ping_name),
                        "failed to clear ping lifetime storage",
                    );
                    warn_on_error(
                        data.clear_lifetime_storage(Lifetime::User, ping_name),
                        "failed to clear user lifetime storage",
                    );
                    warn_on_error(
                        data.clear_lifetime_storage(Lifetime::Application, ping_name),
                        "failed to clear application lifetime storage",
                    );
                }
            }
        }
        if let Err(err) = self.event_data_store.clear_all() {
            log::warn!("Error clearing pending events: {}", err);
        }

        // The persisted `session#*` keys were cleared with internal storage above.
        // Reset the in-memory session state to match, so a later opt-in can't
        // continue the pre-opt-out session. No `session_end` is recorded, since
        // upload is being disabled.
        self.session_manager = SessionManager::new(
            self.session_manager.mode,
            self.session_manager.configured_sample_rate,
            self.session_manager.inactivity_timeout,
        );

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

    /// Gets a reference to the session manager.
    pub fn session_manager(&self) -> &SessionManager {
        &self.session_manager
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

    /// Gets the number of "events" pings to accelerate each session, plus one.
    pub fn get_events_ping_acceleration_factor(&self) -> usize {
        let remote_settings_config = self.remote_settings_config.lock().unwrap();

        if let Some(factor) = remote_settings_config.events_ping_acceleration_factor {
            factor
        } else {
            self.events_ping_acceleration_factor.unwrap_or(1)
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
        let config_value = {
            // Hold the lock while merging config and serializing, then release
            // before performing IO in set_sync.
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

            // Clamp to [0.0, 1.0] so callers can't accidentally set an invalid rate.
            //
            // NOTE: `session_sample_rate` is intentionally NOT applied to any
            // currently-active session.  The override is picked up at the next
            // `session_start()` call.  This "sticky per session" design means:
            //   - A mid-session RS rollout does not change sampling mid-flight,
            //     which would otherwise cause partial session data.
            //   - To clear the override and revert to the configured rate, set
            //     `session_sample_rate` to `null` in the RS payload.  The next
            //     session will use `configured_sample_rate` as the fallback.
            //
            // This override is intentionally NOT persisted to storage.  Remote
            // Settings configuration is refreshed on every app startup, so the
            // override will be re-applied before the next session begins.
            // Persisting it would risk making a stale value sticky if the RS
            // payload changes or is removed between restarts.
            remote_settings_config.session_sample_rate = cfg.session_sample_rate.map(|r| {
                let clamped = r.clamp(0.0, 1.0);
                if clamped != r {
                    log::warn!(
                        "session_sample_rate {} out of range, clamped to {}",
                        r,
                        clamped
                    );
                }
                clamped
            });

            remote_settings_config.events_ping_acceleration_factor =
                cfg.events_ping_acceleration_factor;

            // Store the Server Knobs configuration as an ObjectMetric
            // Since RemoteSettingsConfig only contains maps with string keys and primitives,
            // serialization via the derived Serialize impl cannot fail so it is safe to unwrap.
            serde_json::to_value(&*remote_settings_config).unwrap()
        };

        self.additional_metrics
            .server_knobs_config
            .set_sync(self, config_value);

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

    /// Set early metrics unconditionally.
    ///
    /// This covers e.g. the `first_run_date` and application lifetime metrics known at build time.
    fn set_early_metrics(&mut self) {
        // We set the first_run_date unconditionally nowadays
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
    /// (`GLEAN_DEBUG_VIEW_TAG`) or through the [`set_debug_view_tag`](Glean::set_debug_view_tag) function.
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
    /// or through the [`set_source_tags`](Glean::set_source_tags) function.
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
        match self.storage().get_metric(
            #[cfg(not(feature = "sqlite"))]
            self,
            dirty_bit_metric.meta(),
            INTERNAL_STORAGE,
        ) {
            Some(Metric::Boolean(b)) => b,
            _ => false,
        }
    }

    // -----------------------------------------------------------------------
    // Session lifecycle methods
    // -----------------------------------------------------------------------

    /// Restores session state from persistent storage at startup.
    ///
    /// Must be called after `data_store` is initialized (i.e. after
    /// `Database::new` succeeds) so that the storage reads are valid.
    ///
    /// **Sequence counter**: `session_seq` is always restored so it is
    /// monotonically increasing across restarts.  Note that if a crash occurs
    /// between `store_session_seq` and `persist_session_id` inside
    /// `session_start`, the sequence number will have been incremented but no
    /// session ID will be persisted.  On the next restart this method will
    /// restore the incremented seq and the next session will be assigned
    /// seq+1, leaving a one-element gap.  This is acceptable — downstream
    /// analysts should treat sequence numbers as monotonically non-decreasing,
    /// not strictly contiguous.
    ///
    /// **AUTO mode resumption**: requires both a persisted `session_id` **and**
    /// an `inactive_since` timestamp.  If either is absent the previous session
    /// is considered abandoned and the next `handle_client_active` call will
    /// start a fresh session via `session_start()`.  On a crash restart,
    /// `recover_session_on_dirty_flag()` overwrites whatever this method
    /// restores, so the dirty-flag path is always authoritative.
    fn restore_session_state_from_storage(&mut self) {
        // Always restore seq so new sessions increment from the last known value.
        self.session_manager.session_seq = session::read_session_seq(self);

        // Check for an orphaned session from a previous build that used a
        // different SessionMode.  If the current mode would not restore the
        // persisted session, emit a synthetic session_end("abandoned") and
        // clear all persisted session state so it doesn't leak across builds.
        if self.session_manager.mode != SessionMode::Auto {
            if let Some(id_str) = session::read_session_id(self) {
                log::info!(
                    "Orphaned session {} found from a previous Auto-mode build; \
                     emitting session_end(\"abandoned\") and clearing storage",
                    id_str
                );
                let seq = self.session_manager.session_seq;
                self.record_session_end_event(&id_str, seq, Some("abandoned"));
                session::clear(self);
            }
            return;
        }

        // AUTO mode: restore inactive session state so inactivity timeout
        // evaluation can happen lazily on the next handle_client_active call.
        if let Some(inactive_since) = session::read_inactive_since(self) {
            if let Some(id_str) = session::read_session_id(self) {
                if let Ok(id) = Uuid::parse_str(&id_str) {
                    // Recompute sampled_in deterministically from the UUID so
                    // the sampling decision is consistent across the resumed session.
                    let sampled_in = session::uuid_to_sample_value(&id)
                        < self.session_manager.configured_sample_rate;
                    self.session_manager.session_id = Some(id);
                    self.session_manager.inactive_since = Some(inactive_since);
                    self.session_manager.sampled_in = sampled_in;
                    self.session_manager.session_start_time =
                        session::read_session_start_time(self);
                    if self.session_manager.session_start_time.is_none() {
                        log::warn!(
                            "Resumed session {} has no persisted session_start_time; \
                             events in this session will carry session_start_time: null",
                            id
                        );
                    }
                    // Restore event_seq so the resumed session issues
                    // monotonically increasing sequence numbers even across
                    // a clean restart.
                    self.session_manager
                        .event_seq
                        .store(session::read_session_event_seq(self), Ordering::Relaxed);
                    self.session_manager.state = SessionState::Inactive;
                }
            }
        }
    }

    /// Injects a `glean_timestamp` key into `extra` when event timestamps are enabled.
    ///
    /// Uses the wall clock, matching `EventMetric::record`. The event's main
    /// `timestamp` stays on the monotonic clock.
    fn maybe_inject_glean_timestamp(&self, extra: &mut std::collections::HashMap<String, String>) {
        if self.with_timestamps {
            let precise_timestamp = Utc::now().timestamp_millis() as u64;
            extra.insert("glean_timestamp".to_string(), precise_timestamp.to_string());
        }
    }

    /// Records a `glean.session_start` boundary event (always, regardless of sampling).
    fn record_session_start_event(
        &self,
        session_id: &str,
        seq: u64,
        start_time: DateTime<FixedOffset>,
        sampled_in: bool,
    ) {
        let meta = CommonMetricData {
            name: "session_start".into(),
            category: "glean".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        };
        let timestamp = crate::get_timestamp_ms();
        let mut extra = std::collections::HashMap::new();
        extra.insert("session_id".to_string(), session_id.to_string());
        extra.insert("session_seq".to_string(), seq.to_string());
        extra.insert(
            "session_start_time".to_string(),
            start_time.to_rfc3339_opts(SecondsFormat::Millis, true),
        );
        extra.insert("sampled_in".to_string(), sampled_in.to_string());
        self.maybe_inject_glean_timestamp(&mut extra);
        self.event_data_store.record(
            self,
            &meta.into(),
            timestamp,
            Some(extra),
            EventSessionContext::OutOfSession,
        );
    }

    /// Records a `glean.session_end` boundary event (always, regardless of sampling).
    fn record_session_end_event(&self, session_id: &str, seq: u64, reason: Option<&str>) {
        let meta = CommonMetricData {
            name: "session_end".into(),
            category: "glean".into(),
            send_in_pings: vec!["events".into()],
            lifetime: Lifetime::Ping,
            ..Default::default()
        };
        let timestamp = crate::get_timestamp_ms();
        let mut extra = std::collections::HashMap::new();
        extra.insert("session_id".to_string(), session_id.to_string());
        extra.insert("session_seq".to_string(), seq.to_string());
        if let Some(r) = reason {
            extra.insert("reason".to_string(), r.to_string());
        }
        self.maybe_inject_glean_timestamp(&mut extra);
        self.event_data_store.record(
            self,
            &meta.into(),
            timestamp,
            Some(extra),
            EventSessionContext::OutOfSession,
        );
    }

    /// Starts a new session, persists state, and records a boundary event.
    ///
    /// If a session is already active it is ended cleanly before the new one
    /// starts, preventing orphaned sessions with no corresponding `session_end`.
    pub fn session_start(&mut self) {
        // End any already-active session so we never orphan a session_end event.
        if self.session_manager.is_active() {
            self.session_end(Some("replaced"));
        }

        // 1. Compute new seq from in-memory value (authoritative after init).
        let new_seq = self.session_manager.session_seq + 1;

        // 2. Generate new session_id and compute sampling.
        //    Prefer a remote-settings override if one has been set, falling back
        //    to the immutable configured_sample_rate (never the last effective
        //    rate) so RS overrides can be fully cleared without residual effects.
        //    The rate is sampled once here and is sticky for the entire session;
        //    any RS update received mid-session takes effect at the next session_start.
        let session_id = uuid::Uuid::new_v4();
        let sample_rate = {
            let remote = self.remote_settings_config.lock().unwrap();
            remote
                .session_sample_rate
                .unwrap_or(self.session_manager.configured_sample_rate)
        };
        let sampled_in = session::uuid_to_sample_value(&session_id) < sample_rate;

        // 3. Update in-memory state.
        self.session_manager.sample_rate = sample_rate;
        // Truncate to millisecond precision so that in-memory and persisted
        // (RFC 3339 millis) representations are identical after a round-trip.
        let start_time = {
            let now = local_now_with_offset();
            let millis = now.timestamp_millis();
            DateTime::from_timestamp_millis(millis)
                .expect("valid timestamp")
                .with_timezone(now.offset())
        };
        self.session_manager.session_start_time = Some(start_time);
        self.session_manager.session_id = Some(session_id);
        self.session_manager.session_seq = new_seq;
        self.session_manager.event_seq.store(0, Ordering::Relaxed);
        self.session_manager.sampled_in = sampled_in;
        self.session_manager.state = SessionState::Active;
        self.session_manager.inactive_since = None;

        // 4. Persist to storage.
        session::store_session_seq(self, new_seq);
        session::persist_session_id(self, &session_id.to_string());
        session::persist_session_start_time(self, start_time);
        session::clear_inactive_since(self);

        // 5. Increment diagnostic counter.
        self.additional_metrics.sessions_seen.add_sync(self, 1);

        // 6. Record boundary event.
        self.record_session_start_event(&session_id.to_string(), new_seq, start_time, sampled_in);
    }

    /// Ends the current session, persists state, and records a boundary event.
    ///
    /// Returns the ended session's metadata, or `None` if no session was active.
    pub fn session_end(&mut self, reason: Option<&str>) -> Option<crate::session::SessionMetadata> {
        if self.session_manager.state != SessionState::Active {
            return None;
        }

        let session_id = self.session_manager.session_id?;
        let seq = self.session_manager.session_seq;
        let event_seq = self.session_manager.event_seq.load(Ordering::Relaxed);
        let sample_rate = self.session_manager.sample_rate;
        let start_time = self.session_manager.session_start_time;

        // Clear persistence.
        session::clear(self);

        // Reset in-memory state so the next session_start gets a clean slate.
        self.session_manager.reset_state();

        // Record boundary event.
        self.record_session_end_event(&session_id.to_string(), seq, reason);

        Some(crate::session::SessionMetadata {
            session_id: session_id.to_string(),
            session_seq: seq,
            event_seq,
            session_sample_rate: sample_rate,
            session_start_time: start_time.map(|t| t.to_rfc3339_opts(SecondsFormat::Millis, true)),
        })
    }

    /// Transitions the current session to inactive (AUTO mode).
    ///
    /// Records the `inactive_since` timestamp for timeout evaluation on next activation.
    /// Does NOT end the session — that happens lazily on next `handle_client_active`.
    pub(crate) fn session_transition_to_inactive(&mut self) {
        if self.session_manager.state != SessionState::Active {
            return;
        }

        let now = local_now_with_offset();
        // Snapshot event_seq before changing state so the value is stable.
        let event_seq = self.session_manager.event_seq.load(Ordering::Relaxed);
        self.session_manager.state = SessionState::Inactive;
        self.session_manager.inactive_since = Some(now);

        // Persist for crash recovery and clean-restart resumption.
        // event_seq is persisted here (rather than on every increment) because
        // this is the only point where events stop being recorded mid-session;
        // if the app crashes before the next activation, the recovered session
        // will at least have the correct seq baseline from the last inactive
        // transition.
        session::persist_inactive_since(self, now);
        session::store_session_event_seq(self, event_seq);
    }

    /// Handles transitioning from inactive to active (AUTO mode).
    ///
    /// Evaluates the inactivity timeout:
    /// - If the timeout has NOT expired: resume the existing session.
    /// - If the timeout HAS expired: end the old session and start a new one.
    ///
    /// Returns `true` if a new session was started.
    pub(crate) fn session_transition_to_active(&mut self) -> bool {
        match self.session_manager.inactive_since {
            None => {
                // No inactive_since recorded: treat as a cold activation and start
                // a fresh session.  The call site in handle_client_active guards
                // with `inactive_since.is_some()` so this is normally unreachable,
                // but we handle it safely rather than leaving state inconsistent.
                self.session_start();
                true
            }
            Some(inactive_since) => {
                let now = local_now_with_offset();
                let elapsed = (now - inactive_since).to_std().unwrap_or_default();

                // A timeout of zero means "never time out" (session always resumes).
                if !self.session_manager.inactivity_timeout.is_zero()
                    && elapsed >= self.session_manager.inactivity_timeout
                {
                    // Timeout expired → end old session (emits boundary event), start new one.
                    // The session state was set to Inactive by session_transition_to_inactive(),
                    // but session_id is still set. Restore Active so session_end() can proceed.
                    self.session_manager.state = SessionState::Active;
                    self.session_end(Some("timeout"));
                    self.session_start();
                    true
                } else {
                    // Timeout has NOT expired → resume existing session.
                    self.session_manager.state = SessionState::Active;
                    self.session_manager.inactive_since = None;
                    session::clear_inactive_since(self);
                    false
                }
            }
        }
    }

    /// Called during initialization to recover an abnormally terminated session.
    ///
    /// If the dirty flag was set and a session ID is persisted, emits a synthetic
    /// `session_end` event with reason "abnormal" and clears session state.
    pub(crate) fn recover_session_on_dirty_flag(&mut self) {
        let persisted_id = match session::read_session_id(self) {
            Some(id) => id,
            None => return, // No previous session to recover.
        };

        let persisted_seq = self.session_manager.session_seq;
        let inactive_since = session::read_inactive_since(self);

        // Determine if the session ended while inactive (timeout may have expired).
        let reason = if inactive_since.is_some() {
            "abnormal_inactive"
        } else {
            "abnormal"
        };

        log::info!(
            "Recovering abnormally terminated session: {} (seq={})",
            persisted_id,
            persisted_seq
        );

        // Emit synthetic session_end.
        self.record_session_end_event(&persisted_id, persisted_seq, Some(reason));

        // Clear persisted session state so the recovered session won't be replayed.
        session::clear(self);

        // Reset in-memory state so the next session_start gets a clean slate.
        self.session_manager.reset_state();
    }

    // -----------------------------------------------------------------------
    // Client lifecycle methods
    // -----------------------------------------------------------------------

    /// Performs the collection/cleanup operations required by becoming active.
    ///
    /// This functions generates a baseline ping with reason `active`
    /// and then sets the dirty bit.
    pub fn handle_client_active(&mut self) {
        match self.session_manager.mode {
            SessionMode::Auto => {
                if !self.session_manager.is_active() {
                    if self.session_manager.inactive_since.is_some() {
                        // Was inactive — evaluate timeout.
                        self.session_transition_to_active();
                    } else {
                        // First activation — start initial session.
                        self.session_start();
                    }
                }
            }
            SessionMode::Lifecycle => {
                // Only start a session on the first activation following an inactive
                // transition. Guard against duplicate handle_client_active calls which
                // are not a real lifecycle transition.
                if !self.session_manager.is_active() {
                    self.session_start();
                }
            }
            SessionMode::Manual => {
                // No automatic session management.
            }
        }

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
        match self.session_manager.mode {
            SessionMode::Auto => {
                // In AUTO mode, don't end the session immediately. Instead record
                // inactive_since for lazy timeout evaluation on next activation.
                self.session_transition_to_inactive();
            }
            SessionMode::Lifecycle => {
                // End session immediately on going inactive.
                self.session_end(Some("inactive"));
            }
            SessionMode::Manual => {
                // No automatic session management.
            }
        }

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

    /// Clears the core attribution data.
    /// Does not clear glean.attribution.ext.
    pub fn clear_attribution(&self) {
        if let Some(data) = self.data_store.as_ref() {
            [
                &self.core_metrics.attribution_source,
                &self.core_metrics.attribution_medium,
                &self.core_metrics.attribution_campaign,
                &self.core_metrics.attribution_term,
                &self.core_metrics.attribution_content,
            ]
            .iter()
            .for_each(|metric| {
                let meta = metric.meta();
                _ = data.remove_single_metric(
                    meta.inner.lifetime,
                    &meta.storage_names()[0],
                    &meta.base_identifier(),
                );
            });
        }
    }

    /// Updates attribution fields with new values.
    /// AttributionMetrics fields with `None` values will not overwrite older values.
    pub fn update_attribution(&self, attribution: AttributionMetrics) {
        const MAX_ATTRIBUTION_LENGTH: usize = 512;

        if let Some(source) = attribution.source {
            self.core_metrics
                .attribution_source
                .set_sync_with_max_length(self, source, MAX_ATTRIBUTION_LENGTH);
        }
        if let Some(medium) = attribution.medium {
            self.core_metrics
                .attribution_medium
                .set_sync_with_max_length(self, medium, MAX_ATTRIBUTION_LENGTH);
        }
        if let Some(campaign) = attribution.campaign {
            self.core_metrics
                .attribution_campaign
                .set_sync_with_max_length(self, campaign, MAX_ATTRIBUTION_LENGTH);
        }
        if let Some(term) = attribution.term {
            self.core_metrics.attribution_term.set_sync_with_max_length(
                self,
                term,
                MAX_ATTRIBUTION_LENGTH,
            );
        }
        if let Some(content) = attribution.content {
            self.core_metrics
                .attribution_content
                .set_sync_with_max_length(self, content, MAX_ATTRIBUTION_LENGTH);
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

    /// Clears the core distribution data.
    /// Does not clear glean.distribution.ext.
    pub fn clear_distribution(&self) {
        if let Some(data) = self.data_store.as_ref() {
            let meta = self.core_metrics.distribution_name.meta();
            _ = data.remove_single_metric(
                meta.inner.lifetime,
                &meta.storage_names()[0],
                &meta.base_identifier(),
            );
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
