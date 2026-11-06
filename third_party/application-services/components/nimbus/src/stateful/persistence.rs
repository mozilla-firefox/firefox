/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Our storage abstraction, currently backed by Rkv.

use std::fmt;
use std::fs;
use std::path::Path;
use std::sync::Arc;

use rkv::backend::{
    SafeMode, SafeModeDatabase, SafeModeEnvironment, SafeModeRoCursor, SafeModeRoTransaction,
    SafeModeRwTransaction,
};
use rkv::{StoreError, StoreOptions};

use crate::enrollment::ExperimentEnrollment;
use crate::error::{ErrorCode, NimbusError, Result, debug, info, warn};
use crate::metrics::{DatabaseLoadExtraDef, DatabaseMigrationExtraDef, MetricsHandler};
use crate::stateful::enrollment::v3;

// This uses the lmdb backend for rkv, which is unstable.
// We use it for now since glean didn't seem to have trouble with it (although
// it must be noted that the rkv documentation explicitly says "To use rkv in
// production/release environments at Mozilla, you may do so with the "SafeMode"
// backend", so we really should get more guidance here.)

// We use an incrementing integer to manage database migrations.
// If you need to make a backwards-incompatible change to the data schema,
// increment `DB_VERSION` and implement some migration logic in `maybe_upgrade`.
//
// ⚠️ Warning : Altering the type of `DB_VERSION` would itself require a DB migration. ⚠️
pub(crate) const DB_KEY_DB_VERSION: &str = "db_version";

/// The current database version.
pub(crate) const DB_VERSION: u16 = 4;

pub(crate) const DB_KEY_DB_WAS_CORRUPT: &str = "db-was-corrupt";

/// The minimum database version that will be migrated.
///
/// If the version is below this threshold, the database will be reset.
pub(crate) const DB_MIN_VERSION: u16 = 2;

const RKV_MAX_DBS: u32 = 6;

pub(crate) const DB_KEY_EXPERIMENT_PARTICIPATION: &str = "user-opt-in-experiments";
pub(crate) const DB_KEY_ROLLOUT_PARTICIPATION: &str = "user-opt-in-rollouts";

// Legacy key for migration purposes
pub(crate) const DB_KEY_GLOBAL_USER_PARTICIPATION: &str = "user-opt-in";

pub(crate) const DEFAULT_EXPERIMENT_PARTICIPATION: bool = true;
pub(crate) const DEFAULT_ROLLOUT_PARTICIPATION: bool = true;

pub type Rkv = rkv::Rkv<SafeModeEnvironment>;
pub type RkvSingleStore = rkv::SingleStore<SafeModeDatabase>;
pub type Reader<'t> = rkv::Reader<SafeModeRoTransaction<'t>>;
pub type Writer<'t> = rkv::Writer<SafeModeRwTransaction<'t>>;
pub trait Readable<'r>:
    rkv::Readable<'r, Database = SafeModeDatabase, RoCursor = SafeModeRoCursor<'r>>
{
}
impl<'r, T: rkv::Readable<'r, Database = SafeModeDatabase, RoCursor = SafeModeRoCursor<'r>>>
    Readable<'r> for T
{
}

pub fn rkv_new(path: &Path) -> Result<Rkv, rkv::StoreError> {
    Rkv::with_capacity::<SafeMode>(path, RKV_MAX_DBS)
}

/// Enumeration of the different stores within our database.
///
/// Our rkv database contains a number of different "stores", and the items
/// in each store correspond to a particular type of object at the Rust level.
pub enum StoreId {
    /// Store containing the set of known experiments, as read from the server.
    ///
    /// Keys in the `Experiments` store are experiment identifier slugs, and their
    /// corresponding values are  serialized instances of the [`Experiment`] struct
    /// representing the last known state of that experiment.
    Experiments,
    /// Store containing the set of known experiment enrollments.
    ///
    /// Keys in the `Enrollments` store are experiment identifier slugs, and their
    /// corresponding values are serialized instances of the [`ExperimentEnrollment`]
    /// struct representing the current state of this client's enrollment (or not)
    /// in that experiment.
    Enrollments,
    /// Store containing miscellaneous metadata about this client instance.
    ///
    /// Keys in the `Meta` store are string constants, and their corresponding values
    /// are serialized items whose type depends on the constant. Known constraints
    /// include:
    ///   * "db_version":   u16, the version number of the most revent migration
    ///     applied to this database.
    ///   * "db_was_corrupt":   boolean, whether or not a corrupt database was
    ///     replaced with a new one in Database::open_single
    ///   * "nimbus-id":    String, the randomly-generated identifier for the
    ///     current client instance.
    ///   * "user-opt-in-experiments":  bool, whether the user has explicitly opted in or out
    ///     of participating in experiments.
    ///   * "user-opt-in-rollouts":  bool, whether the user has explicitly opted in or out
    ///     of participating in rollouts.
    ///   * "installation-date": a UTC DateTime string, defining the date the consuming app was
    ///     installed
    ///   * "update-date": a UTC DateTime string, defining the date the consuming app was
    ///     last updated
    ///   * "app-version": String, the version of the app last persisted
    Meta,
    /// Store containing pending updates to experiment data.
    ///
    /// The `Updates` store contains a single key "pending-experiment-updates", whose
    /// corresponding value is a serialized `Vec<Experiment>` of new experiment data
    /// that has been received from the server but not yet processed by the application.
    Updates,
    /// Store containing collected counts of behavior events for targeting purposes.
    ///
    /// Keys in the `EventCounts` store are strings representing the identifier for
    /// the event and their corresponding values represent a serialized instance of a
    /// [`MultiIntervalCounter`] struct that contains a set of configurations and data
    /// for the different time periods that the data will be aggregated on.
    EventCounts,
}

/// A wrapper for an Rkv store. Implemented to allow any value which supports
/// serde to be used.
pub struct SingleStore {
    store: RkvSingleStore,
}

impl SingleStore {
    pub fn new(store: RkvSingleStore) -> Self {
        SingleStore { store }
    }

    pub fn put<T: serde::Serialize + for<'de> serde::Deserialize<'de>>(
        &self,
        writer: &mut Writer,
        key: &str,
        persisted_data: &T,
    ) -> Result<()> {
        let persisted_json = match serde_json::to_string(persisted_data) {
            Ok(v) => v,
            Err(e) => return Err(NimbusError::JSONError("persisted_json = nimbus::stateful::persistence::SingleStore::put::serde_json::to_string".into(), e.to_string()))
        };
        self.store
            .put(writer, key, &rkv::Value::Json(&persisted_json))?;
        Ok(())
    }

    #[allow(dead_code)]
    pub fn delete(&self, writer: &mut Writer, key: &str) -> Result<()> {
        self.store.delete(writer, key)?;
        Ok(())
    }

    pub fn clear(&self, writer: &mut Writer) -> Result<()> {
        self.store.clear(writer)?;
        Ok(())
    }

    // Some "get" functions that cooperate with transactions (ie, so we can
    // get what we've written to the transaction before it's committed).
    // It's unfortunate that these are duplicated with the DB itself, but the
    // traits used by rkv make this tricky.
    pub fn get<'r, T, R>(&self, reader: &'r R, key: &str) -> Result<Option<T>>
    where
        R: Readable<'r>,
        T: serde::Serialize + for<'de> serde::Deserialize<'de>,
    {
        let persisted_data = self.store.get(reader, key)?;
        match persisted_data {
            Some(data) => {
                if let rkv::Value::Json(data) = data {
                    Ok(Some(match serde_json::from_str::<T>(data) {
                        Ok(v) => v,
                        Err(e) => return Err(NimbusError::JSONError("match persisted_data nimbus::stateful::persistence::SingleStore::get::serde_json::from_str".into(), e.to_string()))
                    }))
                } else {
                    Err(NimbusError::InvalidPersistedData)
                }
            }
            None => Ok(None),
        }
    }

    /// Fork of collect_all that simply drops records that fail to read
    /// rather than simply returning an error up the stack.  This likely
    /// wants to be just a parameter to collect_all, but for now....
    pub fn try_collect_all<'r, T, R>(&self, reader: &'r R) -> Result<Vec<T>>
    where
        R: Readable<'r>,
        T: serde::Serialize + for<'de> serde::Deserialize<'de>,
    {
        let mut result = Vec::new();
        let mut iter = self.store.iter_start(reader)?;
        while let Some(Ok((_, data))) = iter.next() {
            if let rkv::Value::Json(data) = data {
                let unserialized = serde_json::from_str::<T>(data);
                match unserialized {
                    Ok(value) => result.push(value),
                    Err(e) => {
                        // If there is an error, we won't push this onto the
                        // result Vec, but we won't blow up the entire
                        // deserialization either.
                        warn!(
                            "try_collect_all: discarded a record while deserializing with: {:?}",
                            e
                        );
                        warn!(
                            "try_collect_all:   data that failed to deserialize: {:?}",
                            data
                        );
                    }
                };
            }
        }
        Ok(result)
    }

    pub fn collect_all<'r, T, R>(&self, reader: &'r R) -> Result<Vec<T>>
    where
        R: Readable<'r>,
        T: serde::Serialize + for<'de> serde::Deserialize<'de>,
    {
        let mut result = Vec::new();
        let mut iter = self.store.iter_start(reader)?;
        while let Some(Ok((_, data))) = iter.next() {
            if let rkv::Value::Json(data) = data {
                result.push(match serde_json::from_str::<T>(data) {
                    Ok(v) => v,
                    Err(e) => return Err(NimbusError::JSONError("rkv::Value::Json(data) nimbus::stateful::persistence::SingleStore::collect_all::serde_json::from_str".into(), e.to_string()))
                });
            }
        }
        Ok(result)
    }
}

pub struct SingleStoreDatabase {
    rkv: Rkv,
    pub(crate) store: SingleStore,
}

impl SingleStoreDatabase {
    /// Function used to obtain a "reader" which is used for read-only transactions.
    pub fn read(&self) -> Result<Reader<'_>> {
        Ok(self.rkv.read()?)
    }

    /// Function used to obtain a "writer" which is used for transactions.
    /// The `writer.commit();` must be called to commit data added via the
    /// writer.
    pub fn write(&self) -> Result<Writer<'_>> {
        Ok(self.rkv.write()?)
    }

    /// Function used to obtain values from the internal store.
    pub fn get<'r, T, R>(&self, reader: &'r R, key: &str) -> Result<Option<T>>
    where
        R: Readable<'r>,
        T: serde::Serialize + for<'de> serde::Deserialize<'de>,
    {
        self.store.get(reader, key)
    }
}

/// Metadata about opening an RKV database.
#[derive(Default)]
pub struct OpenRkvMetadata {
    /// Was the database corrupt? If so, it has been replaced by a new, blank
    /// database.
    pub corrupt: bool,
}

/// Metadata about opening an RKV database and its stores.
///
/// This has more information than [`OpenRkvMetadata`] because the former does
/// not attempt to open any stores.
pub struct OpenMetadata {
    /// Was the database corrupt? If so, it has been replaced by a new, blank
    /// database.
    pub corrupt: bool,

    /// The database version recorded at load time.
    ///
    /// A value of `0` may indicate that no version was recorded, as there was
    /// never a v0 database.
    pub initial_version: u16,
}

#[derive(Default)]
pub struct MigrationMetadata {
    pub initial_version: Option<u16>,
    pub migrated_version: Option<u16>,
    pub mirgation_error: Option<String>,
}

#[derive(Clone, Copy)]
pub enum DatabaseMigrationReason {
    Upgrade,
    InvalidVersion,
}

impl fmt::Display for DatabaseMigrationReason {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(match self {
            Self::Upgrade => "upgrade",
            Self::InvalidVersion => "invalid_version",
        })
    }
}

/// Database used to access persisted data
/// This an abstraction around an Rkv database
/// An instance on this database is created each time the component is loaded
/// if there is persisted data, the `get` functions should retrieve it
pub struct Database {
    rkv: Rkv,
    meta_store: SingleStore,
    experiment_store: SingleStore,
    enrollment_store: SingleStore,
    updates_store: SingleStore,
    event_count_store: SingleStore,

    metrics_handler: Arc<dyn MetricsHandler>,
}

impl Database {
    /// Main constructor for a database
    /// Initiates the Rkv database to be used to retrieve persisted data
    /// # Arguments
    /// - `path`: A path to the persisted data, this is provided by the consuming application
    pub fn new<P: AsRef<Path>>(path: P, metrics_handler: Arc<dyn MetricsHandler>) -> Result<Self> {
        let mut event = DatabaseLoadExtraDef::default();

        let (db, open_metadata) = match Self::open(path, metrics_handler.clone()) {
            Ok(db) => db,
            Err(e) => {
                event.error = Some(e.error_code().to_string());
                metrics_handler.record_database_load(event);
                return Err(e);
            }
        };

        event.initial_version = Some(open_metadata.initial_version);
        event.corrupt = Some(open_metadata.corrupt);

        let migrate_result = db.maybe_upgrade(open_metadata.initial_version);
        match migrate_result {
            Ok(migrated_version) => event.migrated_version = migrated_version,
            Err(ref e) => event.migration_error = Some(e.error_code().to_string()),
        }

        metrics_handler.record_database_load(event);

        migrate_result?;

        Ok(db)
    }

    /// Open a database, creating it if it does not exist.
    fn open<P: AsRef<Path>>(
        path: P,
        metrics_handler: Arc<dyn MetricsHandler>,
    ) -> Result<(Self, OpenMetadata)> {
        let (rkv, open_metadata) = Self::open_rkv(path)?;

        let meta_store = rkv.open_single("meta", StoreOptions::create())?;
        let experiment_store = rkv.open_single("experiments", StoreOptions::create())?;
        let enrollment_store = rkv.open_single("enrollments", StoreOptions::create())?;
        let updates_store = rkv.open_single("updates", StoreOptions::create())?;
        let event_count_store = rkv.open_single("event_counts", StoreOptions::create())?;
        let db = Self {
            rkv,
            meta_store: SingleStore::new(meta_store),
            experiment_store: SingleStore::new(experiment_store),
            enrollment_store: SingleStore::new(enrollment_store),
            updates_store: SingleStore::new(updates_store),
            event_count_store: SingleStore::new(event_count_store),
            metrics_handler,
        };

        let mut writer = db.rkv.write()?;

        let mut open_metadata = OpenMetadata {
            corrupt: open_metadata.corrupt,
            initial_version: db.meta_store.get(&writer, DB_KEY_DB_VERSION)?.unwrap_or(0),
        };

        if !open_metadata.corrupt {
            open_metadata.corrupt = db
                .meta_store
                .get(&writer, DB_KEY_DB_WAS_CORRUPT)?
                .unwrap_or(false);

            if open_metadata.corrupt {
                db.meta_store.delete(&mut writer, DB_KEY_DB_WAS_CORRUPT)?;
                writer.commit()?;
            }
        }

        Ok((db, open_metadata))
    }

    pub(crate) fn open_single<P: AsRef<Path>>(
        path: P,
        store_id: StoreId,
    ) -> Result<SingleStoreDatabase> {
        // `Database::open_single` is used by `get_calculated_attributes` to
        // compute `days_since_update` before the Nimbus SDK has loaded the
        // database. The following `open_rkv` call *will* wipe the database upon
        // encountering corruption, which means when we call `Database::new()`
        // from the client we will not be able to determine that corruption occured.
        //
        // Record the corrupted state into the meta store so that the client
        // will be able to report whether or not the database was actually
        // corrupted at startup, regardless if we already clobbered it.
        let (rkv, open_metadata) = Self::open_rkv(path)?;

        if open_metadata.corrupt {
            let meta = rkv.open_single("meta", StoreOptions::create())?;

            let mut writer = rkv.write()?;
            meta.put(
                &mut writer,
                DB_KEY_DB_WAS_CORRUPT,
                &rkv::Value::Json("true"),
            )?;
            writer.commit()?;
        }

        let store = SingleStore::new(match store_id {
            StoreId::Experiments => rkv.open_single("experiments", StoreOptions::create())?,
            StoreId::Enrollments => rkv.open_single("enrollments", StoreOptions::create())?,
            StoreId::Meta => rkv.open_single("meta", StoreOptions::create())?,
            StoreId::Updates => rkv.open_single("updates", StoreOptions::create())?,
            StoreId::EventCounts => rkv.open_single("event_counts", StoreOptions::create())?,
        });
        Ok(SingleStoreDatabase { rkv, store })
    }

    /// Attempt to upgrade the database.
    ///
    /// If the database is already up-to-date, no operations will be performed.
    /// Otherwise migrations will be applied in order until the database is at
    /// [`DB_VERSION`].
    ///
    /// If an error occurs during migration, the experiments, enrollments, and
    /// meta stores will be cleared.
    fn maybe_upgrade(&self, current_version: u16) -> Result<Option<u16>> {
        debug!("entered maybe upgrade");

        println!("maybe_upgrade from {current_version}");

        if current_version == DB_VERSION {
            return Ok(None);
        }

        let mut writer = self.write()?;

        // An `Err` here means either:
        //
        // - an individual migration failed, in which case the machinery in
        //   [`force_apply_migration`] will have wiped the database in an attempt
        //   to recover; or
        //
        // - the database wipe resulting from a failed migration *also* failed,
        //   in which case there is not really anything we can do.
        let _ = self.apply_migrations(&mut writer, current_version);

        // It is safe to clear the update store (i.e. the pending experiments)
        // on all schema upgrades as it will be re-filled from the server on the
        // next `fetch_experiments()`. The current contents of the update store
        // may cause experiments to not load, or worse, accidentally unenroll.
        self.updates_store.clear(&mut writer)?;
        self.meta_store
            .put(&mut writer, DB_KEY_DB_VERSION, &DB_VERSION)?;
        writer.commit()?;
        debug!("maybe_upgrade: transaction committed");

        Ok(Some(DB_VERSION))
    }

    /// Apply all pending migrations.
    ///
    /// If all migrations apply successfully, the database will have version
    /// [`DB_VERSION`].
    fn apply_migrations(&self, writer: &mut Writer, initial_version: u16) -> Result<()> {
        let mut current_version = initial_version;

        if !(DB_MIN_VERSION..=DB_VERSION).contains(&current_version) {
            let reason = if current_version < DB_MIN_VERSION {
                DatabaseMigrationReason::Upgrade
            } else {
                DatabaseMigrationReason::InvalidVersion
            };

            // We need to force-apply this migration because current_version may be > 2.
            self.force_apply_migration(
                writer,
                |writer| self.migrate_reset_to_v2(writer),
                &mut current_version,
                2,
                reason,
            )?;
        };

        self.apply_migration(
            writer,
            |writer| self.migrate_v2_to_v3(writer),
            &mut current_version,
            3,
            DatabaseMigrationReason::Upgrade,
        )?;

        self.apply_migration(
            writer,
            |writer| self.migrate_v3_to_v4(writer),
            &mut current_version,
            4,
            DatabaseMigrationReason::Upgrade,
        )?;

        Ok(())
    }

    /// Apply a single migration, if it is applicable.
    ///
    /// The result of the migration will be reported via telemetry.
    fn apply_migration(
        &self,
        writer: &mut Writer,
        migration: impl FnOnce(&mut Writer) -> Result<()>,
        from_version: &mut u16,
        to_version: u16,
        reason: DatabaseMigrationReason,
    ) -> Result<()> {
        if *from_version >= to_version {
            return Ok(());
        }

        self.force_apply_migration(writer, migration, from_version, to_version, reason)
    }

    /// Forcibly apply a migration, without taking version constraints into
    /// account.
    fn force_apply_migration(
        &self,
        writer: &mut Writer,
        migration: impl FnOnce(&mut Writer) -> Result<()>,
        from_version: &mut u16,
        to_version: u16,
        reason: DatabaseMigrationReason,
    ) -> Result<()> {
        let mut event = DatabaseMigrationExtraDef {
            from_version: *from_version,
            to_version,
            reason: reason.to_string(),
            error: None,
        };

        if let Err(e) = migration(writer) {
            event.error = Some(e.error_code().to_string());
            self.metrics_handler.record_database_migration(event);

            error_support::report_error!(
                "nimbus-database-migration",
                "Error migrating database from v{} to v{}: {:?}. Wiping experiments and enrollments",
                from_version,
                to_version,
                e
            );

            self.clear_experiments_and_enrollments(writer)?;
            return Err(e);
        }

        self.metrics_handler.record_database_migration(event);
        *from_version = to_version;
        Ok(())
    }

    pub(crate) fn clear_experiments_and_enrollments(
        &self,
        writer: &mut Writer,
    ) -> Result<(), NimbusError> {
        self.experiment_store.clear(writer)?;
        self.enrollment_store.clear(writer)?;
        Ok(())
    }

    pub(crate) fn clear_event_count_data(&self, writer: &mut Writer) -> Result<(), NimbusError> {
        self.event_count_store.clear(writer)?;
        Ok(())
    }

    pub fn migrate_reset_to_v2(&self, writer: &mut Writer) -> Result<()> {
        self.clear_experiments_and_enrollments(writer)?;

        Ok(())
    }

    /// Migrates a v2 database to v3
    ///
    /// Separates global user participation into experiments and rollouts participation.
    /// For privacy: if user opted out globally, they remain opted out of experiments.
    fn migrate_v2_to_v3(&self, writer: &mut Writer) -> Result<()> {
        info!("Upgrading from version 2 to version 3");

        let meta_store = &self.meta_store;

        // Get the old global participation flag
        let old_global_participation = meta_store
            .get::<bool, _>(writer, DB_KEY_GLOBAL_USER_PARTICIPATION)?
            .unwrap_or(true); // Default was true

        // Set new separate flags based on privacy requirements:
        // - If user opted out globally, they stay opted out of experiments
        // - If user opted out globally, they stay opted out of rollouts (per requirement #3)
        meta_store.put(
            writer,
            DB_KEY_EXPERIMENT_PARTICIPATION,
            &old_global_participation,
        )?;
        meta_store.put(
            writer,
            DB_KEY_ROLLOUT_PARTICIPATION,
            &old_global_participation,
        )?;

        // Remove the old global participation key if it exists
        if meta_store
            .get::<bool, _>(writer, DB_KEY_GLOBAL_USER_PARTICIPATION)?
            .is_some()
        {
            meta_store.delete(writer, DB_KEY_GLOBAL_USER_PARTICIPATION)?;
        }

        info!(
            "Migration v2->v3: experiments_participation={}, rollouts_participation={}",
            old_global_participation, old_global_participation
        );

        Ok(())
    }

    fn migrate_v3_to_v4(&self, writer: &mut Writer) -> Result<()> {
        info!("Upgrading from version 3 to version 4");

        let enrollment_store = self.get_store(StoreId::Enrollments);
        let enrollments: Vec<v3::LegacyExperimentEnrollment> =
            enrollment_store.try_collect_all(writer)?;

        let enrollments: Vec<ExperimentEnrollment> =
            enrollments.into_iter().map(Into::into).collect();

        for enrollment in enrollments {
            enrollment_store.put(writer, &enrollment.slug, &enrollment)?;
        }

        Ok(())
    }

    /// Gets a Store object, which used with the writer returned by
    /// `self.write()` to update the database in a transaction.
    pub fn get_store(&self, store_id: StoreId) -> &SingleStore {
        match store_id {
            StoreId::Meta => &self.meta_store,
            StoreId::Experiments => &self.experiment_store,
            StoreId::Enrollments => &self.enrollment_store,
            StoreId::Updates => &self.updates_store,
            StoreId::EventCounts => &self.event_count_store,
        }
    }

    pub fn open_rkv<P: AsRef<Path>>(path: P) -> Result<(Rkv, OpenRkvMetadata)> {
        let mut metadata = OpenRkvMetadata::default();

        let path = std::path::Path::new(path.as_ref()).join("db");
        debug!("open_rkv: path =  {:?}", path.display());
        fs::create_dir_all(&path)?;
        let rkv = match rkv_new(&path) {
            Ok(rkv) => Ok(rkv),
            Err(rkv_error) => {
                match rkv_error {
                    // For some errors we just delete the DB and start again.
                    StoreError::DatabaseCorrupted | StoreError::FileInvalid => {
                        // On one hand this seems a little dangerous, but on
                        // the other hand avoids us knowing about the
                        // underlying implementation (ie, how do we know what
                        // files might exist in all cases?)
                        warn!(
                            "Database at '{}' appears corrupt - removing and recreating",
                            path.display()
                        );
                        fs::remove_dir_all(&path)?;
                        fs::create_dir_all(&path)?;

                        metadata.corrupt = true;

                        rkv_new(&path)
                    }
                    // All other errors are fatal.
                    _ => Err(rkv_error),
                }
            }
        }?;
        debug!("Database initialized");
        Ok((rkv, metadata))
    }

    /// Function used to obtain a "reader" which is used for read-only transactions.
    pub fn read(&self) -> Result<Reader<'_>> {
        Ok(self.rkv.read()?)
    }

    /// Function used to obtain a "writer" which is used for transactions.
    /// The `writer.commit();` must be called to commit data added via the
    /// writer.
    pub fn write(&self) -> Result<Writer<'_>> {
        Ok(self.rkv.write()?)
    }

    /// Function used to retrieve persisted data outside of a transaction.
    /// It allows retrieval of any serializable and deserializable data
    /// Currently only supports JSON data
    // Only available for tests; product code should always be using transactions.
    ///
    /// # Arguments
    /// - `key`: A key for the data stored in the underlying database
    #[cfg(test)]
    pub fn get<T: serde::Serialize + for<'de> serde::Deserialize<'de>>(
        &self,
        store_id: StoreId,
        key: &str,
    ) -> Result<Option<T>> {
        let reader = self.rkv.read()?;
        let persisted_data = self.get_store(store_id).store.get(&reader, key)?;
        match persisted_data {
            Some(data) => {
                if let rkv::Value::Json(data) = data {
                    Ok(Some(match serde_json::from_str::<T>(data) {
                        Ok(v) => v,
                        Err(e) => return Err(NimbusError::JSONError("rkv::Value::Json(data) nimbus::stateful::persistence::Database::get::serde_json::from_str".into(), e.to_string()))
                    }))
                } else {
                    Err(NimbusError::InvalidPersistedData)
                }
            }
            None => Ok(None),
        }
    }

    // Function for collecting all items in a store outside of a transaction.
    // Only available for tests; product code should always be using transactions.
    // Iters are a bit tricky - would be nice to make them generic, but this will
    // do for our use-case.
    #[cfg(test)]
    pub fn collect_all<T: serde::Serialize + for<'de> serde::Deserialize<'de>>(
        &self,
        store_id: StoreId,
    ) -> Result<Vec<T>> {
        let mut result = Vec::new();
        let reader = self.rkv.read()?;
        let mut iter = self.get_store(store_id).store.iter_start(&reader)?;
        while let Some(Ok((_, data))) = iter.next() {
            if let rkv::Value::Json(data) = data {
                result.push(match serde_json::from_str::<T>(data) {
                    Ok(v) => v,
                    Err(e) => return Err(NimbusError::JSONError("rkv::Value::Json(data) nimbus::stateful::persistence::Database::collect_all::serde_json::from_str".into(), e.to_string()))
                });
            }
        }
        Ok(result)
    }
}
