/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{collections::HashMap, sync::Arc};

use error_support::{convert_log_report_error, handle_error};

pub mod client;
pub mod config;
pub mod context;
pub mod error;
pub mod schema;
pub mod service;
#[cfg(feature = "signatures")]
pub(crate) mod signatures;
pub mod storage;
pub mod telemetry;

pub(crate) mod jexl_filter;
mod macros;

pub use client::{Attachment, RemoteSettingsRecord, RsJsonObject};
pub use config::{BaseUrl, RemoteSettingsConfig, RemoteSettingsServer};
pub use context::RemoteSettingsContext;
pub use error::{trace, ApiResult, RemoteSettingsError, Result};
pub use telemetry::{RemoteSettingsTelemetry, SyncStatus, UptakeEventExtras};

use error::Error;
use storage::Storage;
use telemetry::RemoteSettingsTelemetryWrapper;

uniffi::setup_scaffolding!("remote_settings");

/// Application-level Remote Settings manager.
///
/// This handles application-level operations, like syncing all the collections, and acts as a
/// factory for creating clients.
#[derive(uniffi::Object)]
pub struct RemoteSettingsService {
    // This struct adapts server::RemoteSettingsService into the public API
    internal: service::RemoteSettingsService,
}

#[uniffi::export]
impl RemoteSettingsService {
    /// Construct a [RemoteSettingsService]
    ///
    /// This is typically done early in the application-startup process.
    ///
    /// This method performs no IO or network requests and is safe to run in a main thread that
    /// can't be blocked.
    ///
    /// `storage_dir` is a directory to store SQLite files in -- one per collection. If the
    /// directory does not exist, it will be created when the storage is first used. Only the
    /// directory and the SQLite files will be created, any parent directories must already exist.
    #[uniffi::constructor]
    pub fn new(storage_dir: String, config: RemoteSettingsConfig) -> Self {
        Self {
            internal: service::RemoteSettingsService::new(storage_dir, config),
        }
    }

    /// Create a new Remote Settings client
    ///
    /// This method performs no IO or network requests and is safe to run in a main thread that can't be blocked.
    pub fn make_client(&self, collection_name: String) -> Arc<RemoteSettingsClient> {
        self.internal.make_client(collection_name)
    }

    /// Sync collections for all active clients
    ///
    /// The returned list is the list of collections for which updates were seen
    /// and then synced.
    #[handle_error(Error)]
    pub fn sync(&self) -> ApiResult<Vec<String>> {
        self.internal.sync()
    }

    /// Update the remote settings config
    ///
    /// This will cause all current and future clients to use new config and will delete any stored
    /// records causing the clients to return new results from the new config.
    ///
    /// Only intended for QA/debugging.  Swapping the remote settings server in the middle of
    /// execution can cause weird effects.
    #[handle_error(Error)]
    pub fn update_config(&self, config: RemoteSettingsConfig) -> ApiResult<()> {
        self.internal.update_config(config)
    }

    pub fn client_url(&self) -> String {
        self.internal.client_url().to_string()
    }
}

#[cfg_attr(feature = "telemetry-submission", uniffi::export)]
impl RemoteSettingsService {
    /// Set the telemetry implementation used to record Glean metrics.
    /// This should be set to a real implementation (eg. Kotlin, Swift).
    /// If not set, all metric recording is a no-op.
    pub fn set_telemetry(&self, telemetry: Arc<dyn RemoteSettingsTelemetry>) {
        self.internal
            .set_telemetry(RemoteSettingsTelemetryWrapper::new(telemetry));
    }
}

/// Client for a single Remote Settings collection
///
/// Use [RemoteSettingsService::make_client] to create these.
#[derive(uniffi::Object)]
pub struct RemoteSettingsClient {
    // This struct adapts client::RemoteSettingsClient into the public API
    internal: client::RemoteSettingsClient,
}

#[uniffi::export]
impl RemoteSettingsClient {
    /// Collection this client is for
    pub fn collection_name(&self) -> String {
        self.internal.collection_name().to_owned()
    }

    /// Get the current set of records.
    ///
    /// This method normally fetches records from the last sync.  This means that it returns fast
    /// and does not make any network requests.
    ///
    /// If records have not yet been synced it will return None.  Use `sync_if_empty = true` to
    /// change this behavior and perform a network request in this case.  That this is probably a
    /// bad idea if you want to fetch the setting in application startup or when building the UI.
    ///
    /// None will also be returned on disk IO errors or other unexpected errors.  The reason for
    /// this is that there is not much an application can do in this situation other than fall back
    /// to the same default handling as if records have not been synced.
    ///
    /// Application-services schedules regular dumps of the server data for specific collections.
    /// For these collections, `get_records` will never return None.  If you would like to add your
    /// collection to this list, please reach out to the DISCO team.
    #[uniffi::method(default(sync_if_empty = false))]
    pub fn get_records(&self, sync_if_empty: bool) -> Option<Vec<RemoteSettingsRecord>> {
        match self.internal.get_records(sync_if_empty) {
            Ok(records) => records,
            Err(e) => {
                // Log/report the error
                trace!("get_records error: {e}");
                convert_log_report_error(e);
                // Throw away the converted result and return None, there's nothing a client can
                // really do with an error except treat it as the None case
                None
            }
        }
    }

    /// Get the current set of records as a map of record_id -> record.
    ///
    /// See [Self::get_records] for an explanation of when this makes network requests, error
    /// handling, and how the `sync_if_empty` param works.
    #[uniffi::method(default(sync_if_empty = false))]
    pub fn get_records_map(
        &self,
        sync_if_empty: bool,
    ) -> Option<HashMap<String, RemoteSettingsRecord>> {
        self.get_records(sync_if_empty)
            .map(|records| records.into_iter().map(|r| (r.id.clone(), r)).collect())
    }

    /// Returns the last_modified value for the collection as an unsigned int64.
    #[uniffi::method()]
    pub fn get_last_modified_timestamp(&self) -> Option<u64> {
        self.internal
            .get_last_modified_timestamp()
            .unwrap_or_default()
    }

    /// Get attachment data for a remote settings record
    ///
    /// Attachments are large binary blobs used for data that doesn't fit in a normal record.  They
    /// are handled differently than other record data:
    ///
    ///   - Attachments are not downloaded in [RemoteSettingsService::sync]
    ///   - This method will make network requests if the attachment is not cached
    ///   - This method will throw if there is a network or other error when fetching the
    ///     attachment data.
    #[handle_error(Error)]
    pub fn get_attachment(&self, record: &RemoteSettingsRecord) -> ApiResult<Vec<u8>> {
        self.internal.get_attachment(record)
    }

    #[handle_error(Error)]
    pub fn sync(&self) -> ApiResult<()> {
        self.internal.sync()
    }

    #[handle_error(Error)]
    pub fn reset_storage(&self) -> ApiResult<()> {
        self.internal.reset_storage()
    }

    /// Shutdown the client, releasing the SQLite connection used to cache records.
    pub fn shutdown(&self) {
        self.internal.shutdown()
    }
}

impl RemoteSettingsClient {
    /// Create a new client.  This is not exposed to foreign code, consumers need to call
    /// [RemoteSettingsService::make_client]
    fn new(
        base_url: BaseUrl,
        bucket_name: String,
        collection_name: String,
        #[allow(unused)] context: Option<RemoteSettingsContext>,
        storage: Storage,
    ) -> Self {
        Self {
            internal: client::RemoteSettingsClient::new(
                base_url,
                bucket_name,
                collection_name,
                context,
                storage,
            ),
        }
    }
}
