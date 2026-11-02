/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::collections::HashMap;
use std::time::SystemTime;
use sync15::{DeviceType, EngineChoicesModel};

#[derive(Debug, uniffi::Record)]
pub struct SyncParams {
    /// Why are we performing this sync?
    pub reason: SyncReason,
    /// Which engines should we sync?
    pub engines: SyncEngineSelection,
    /// How the user's choice of engines to sync is modelled.
    pub engine_choices_model: EngineChoicesModel,
    /// Keys to encrypt/decrypt data from local database files.  These are
    /// separate from the key we use to encrypt the sync payload as a whole.
    pub local_encryption_keys: HashMap<String, String>,
    /// Authorization for the sync server
    pub auth_info: SyncAuthInfo,
    /// An opaque string, as returned in the previous sync's SyncResult and
    /// persisted to disk, or null if no such state is available. This includes
    /// information such as the list of engines previously enabled, certain
    /// server timestamps and GUIDs etc. If this value isn't correctly persisted
    /// and round-tripped, each sync may look like a "first sync".
    pub persisted_state: Option<String>,
    /// Information about the current device, such as its name, formfactor and
    /// FxA device ID.
    pub device_settings: DeviceSettings,
}

#[derive(Debug, uniffi::Enum)]
pub enum SyncReason {
    Scheduled,
    User,
    PreSleep,
    Startup,
    EnabledChange,
    Backgrounded,
}

#[derive(Debug, uniffi::Enum)]
pub enum SyncEngineSelection {
    All,
    Some { engines: Vec<String> },
}

#[derive(Debug, uniffi::Record)]
pub struct SyncAuthInfo {
    pub kid: String,
    pub fxa_access_token: String,
    pub sync_key: String,
    pub tokenserver_url: String,
}

#[derive(Debug, uniffi::Record)]
pub struct DeviceSettings {
    pub fxa_device_id: String,
    pub name: String,
    pub kind: DeviceType,
    /// The engines this device syncs, to be published for other devices on the
    /// account to see. Not yet published.
    #[uniffi(default)]
    pub enabled_engines: Vec<String>,
}

#[derive(Debug, uniffi::Record)]
pub struct SyncResult {
    /// Result from the sync server
    pub status: ServiceStatus,
    /// Engines that synced successfully
    pub successful: Vec<String>,
    /// Maps the names of engines that failed to sync to the reason why
    pub failures: HashMap<String, String>,
    /// State that should be persisted to disk and supplied to the sync method
    /// on the next sync (See SyncParams.persisted_state).
    pub persisted_state: String,
    /// The list of engines which are marked as "declined" (ie, disabled) on the
    /// sync server. The list of declined engines is global to the account
    /// rather than to the device. Apps should use this after every sync to
    /// update the local state (ie, to ensure that their Sync UI correctly
    /// reflects what engines are enabled and disabled), because these could
    /// change after every sync.
    pub declined: Option<Vec<String>>,
    /// Earliest time that the next sync should happen at
    pub next_sync_allowed_at: Option<SystemTime>,
    /// JSON string encoding a `SyncTelemetryPing` object
    pub telemetry_json: Option<String>,
}

#[derive(Debug, uniffi::Enum)]
pub enum ServiceStatus {
    Ok,
    NetworkError,
    ServiceError,
    AuthError,
    BackedOff,
    OtherError,
}

impl ServiceStatus {
    pub fn is_ok(&self) -> bool {
        matches!(self, ServiceStatus::Ok)
    }
}
