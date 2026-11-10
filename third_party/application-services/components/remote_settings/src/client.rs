/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::config::BaseUrl;
use crate::error::{breadcrumb, debug, trace, Error, Result};
use crate::jexl_filter::JexlFilter;
#[cfg(feature = "signatures")]
use crate::signatures;
use crate::storage::Storage;
use crate::RemoteSettingsContext;
use crate::{packaged_attachments, packaged_collections, RemoteSettingsServer};
use parking_lot::{Mutex, MutexGuard};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::time::{Duration, Instant};
use url::Url;
use viaduct::{Request, Response};

#[cfg(feature = "signatures")]
#[cfg(not(test))]
use std::time::{SystemTime, UNIX_EPOCH};

#[cfg(feature = "signatures")]
#[cfg(not(test))]
fn epoch_seconds() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap() // Time won't go backwards.
        .as_secs()
}

#[cfg(feature = "signatures")]
#[cfg(test)]
thread_local! {
    static MOCK_TIME: std::cell::Cell<Option<u64>> = const { std::cell::Cell::new(None) }
}

#[cfg(feature = "signatures")]
#[cfg(test)]
fn epoch_seconds() -> u64 {
    MOCK_TIME.with(|mock_time| mock_time.get().unwrap_or(0))
}

const HEADER_BACKOFF: &str = "Backoff";
const HEADER_RETRY_AFTER: &str = "Retry-After";

/// Hard-coded SHA256 of our root certificates. This is used by rc_crypto/pkixc to verify that the
/// certificates chains used in content signatures verification were produced from our root certificate.
/// See https://bugzilla.mozilla.org/show_bug.cgi?id=1940903 to align with desktop implementation.
#[cfg(feature = "signatures")]
const ROOT_CERT_SHA256_HASH_PROD: &str = "C8:A8:0E:9A:FA:EF:4E:21:9B:6F:B5:D7:A7:1D:0F:10:12:23:BA:C5:00:1A:C2:8F:9B:0D:43:DC:59:A1:06:DB";
#[cfg(feature = "signatures")]
const ROOT_CERT_SHA256_HASH_NONPROD: &str = "3C:01:44:6A:BE:90:36:CE:A9:A0:9A:CA:A3:A5:20:AC:62:8F:20:A7:AE:32:CE:86:1C:B2:EF:B7:0F:A0:C7:45";

#[derive(Debug, Clone, Deserialize)]
struct CollectionData {
    data: Vec<RemoteSettingsRecord>,
    timestamp: u64,
}

/// Internal Remote settings client API
///
/// This stores an ApiClient implementation.  In the real-world, this is always ViaductApiClient,
/// but the tests use a mock client.
pub struct RemoteSettingsClient<C = ViaductApiClient> {
    // This is immutable, so it can be outside the mutex
    collection_name: String,
    inner: Mutex<RemoteSettingsClientInner<C>>,
    // Config that we got from `update_config`.  This should be applied to
    // `RemoteSettingsClientInner` the next time it's used.
    pending_config: Mutex<Option<RemoteSettingsClientConfig>>,
}

struct RemoteSettingsClientInner<C> {
    storage: Storage,
    api_client: C,
    jexl_filter: JexlFilter,
}

struct RemoteSettingsClientConfig {
    server_url: BaseUrl,
    bucket_name: String,
    context: Option<RemoteSettingsContext>,
}

// To initially download the dump (and attachments, if any), run:
//   $ cargo remote-settings dump-get --bucket main --collection-name <collection name>
//
// Then add the entry here.
//
// For subsequent updates, run the command above again.
impl<C: ApiClient> RemoteSettingsClient<C> {
    // One line per bucket + collection
    packaged_collections! {
        ("main", "regions"),
        ("main", "search-config-icons"),
        ("main", "search-config-v2"),
        ("main", "search-config-v3"),
        ("main", "search-telemetry-v2"),
        ("main", "summarizer-models-config"),
        ("main", "translations-models"),
        ("main", "translations-wasm"),
    }

    // You have to specify
    // - bucket + collection_name: ("main", "regions")
    // - One line per file you want to add (e.g. "world")
    //
    // This will automatically also include the NAME.meta.json file
    // for internal validation against hash and size
    //
    // The entries line up with the `Attachment::filename` field,
    // and check for the folder + name in
    // `remote_settings/dumps/{bucket}/attachments/{collection}/{filename}
    packaged_attachments! {
        ("main", "regions") => [
            "world",
            "world-buffered",
        ],
        ("main", "search-config-icons") => [
            "001500a9-1a6c-3f5a-ba15-a5f5a075d256",
            "06cf7432-efd7-f244-927b-5e423005e1ea",
            "0a57b0cf-34f0-4d09-96e4-dbd6e3355410",
            "0d7668a8-c3f4-cfee-cbc8-536511528937",
            "0eec5640-6fde-d6fe-322a-c72c6d5bd5a2",
            "101ce01d-2691-b729-7f16-9d389803384b",
            "177aba42-9bed-4078-e36b-580e8794cd7f",
            "25de0352-aabb-d31f-15f7-bf9299fb004c",
            "2bbe48f4-d3b8-c9e0-86e3-a54c37ec3335",
            "2e835b0e-9709-d1bb-9725-87f59f3445ca",
            "2ecca3f8-c1ef-43cc-b053-886d1ae46c36",
            "32d26d19-aeb0-5c01-32e8-f8970be9246f",
            "39d0b17d-c020-4890-932f-83c0f6ed130b",
            "41135a88-093d-4077-873b-9de1ae133427",
            "41f0d805-3775-4988-8d8c-5ad8ccd86d1c",
            "47da97b5-600f-c450-fd15-a52bb2169c11",
            "48c72361-cd67-412e-bd7f-f81a43c10791",
            "4e271681-3e0f-91ac-9750-03f665efc171",
            "50f6171f-8e7a-b41b-862e-f97397038fb2",
            "5203dd03-2c55-4b53-9c60-58258d587be1",
            "5914932e-66ba-4126-8be5-d37beadd9532",
            "5ded611d-44b2-dc46-fd67-fb116888d75d",
            "5e03d6f4-6ee9-8bc8-cf22-7a5f2cf55c41",
            "6644f26f-28ea-4222-929d-5d43a02dae05",
            "6d10d702-7bd6-1452-90a5-3df665a38f66",
            "6e36a151-e4f4-4117-9067-1ca82c47d01a",
            "6f4da442-d31e-28f8-03af-797d16bbdd27",
            "7072564d-a573-4750-bf33-f0a07631c9eb",
            "70fdd651-6c50-b7bb-09ec-7e85da259173",
            "71f41a0c-5b70-4116-b30f-e62089083522",
            "74793ce1-a918-a5eb-d3c0-2aadaff3c88c",
            "74f94dc2-caf6-4b90-b3d2-f3e2f7714d88",
            "764e3b14-fe16-4feb-8384-124c516a5afa",
            "7bf4ca37-e2b8-4d31-a1c3-979bc0e85131",
            "7c81cf98-7c11-4afd-8279-db89118a6dfb",
            "7cb4d88a-d4df-45b2-87e4-f896eaf1bbdb",
            "7edaf4fe-a8a0-432b-86d2-bf75ebe80851",
            "7efbed51-813c-581d-d8d3-f8758434e451",
            "84bb4962-e571-227a-9ef6-2ac5f2aac361",
            "87ac4cde-f581-398b-1e32-eb4079183b36",
            "8831ce10-b1e4-6eb4-4975-83c67457288e",
            "890de5c4-0941-a116-473a-5d240e79497a",
            "8abb10a7-212f-46b5-a7b4-244f414e3810",
            "91a9672d-e945-8e1e-0996-aefdb0190716",
            "94a84724-c30f-4767-ba42-01cc37fc31a4",
            "96327a73-c433-5eb4-a16d-b090cadfb80b",
            "9802e63d-05ec-48ba-93f9-746e0981ad98",
            "9d96547d-7575-49ca-8908-1e046b8ea90e",
            "a06db97d-1210-ea2e-5474-0e2f7d295bfd",
            "a06dc3fd-4bdb-41f3-2ebc-4cbed06a9bd3",
            "a2c7d4e9-f770-51e1-0963-3c2c8401631d",
            "a83f24e4-602c-47bd-930c-ad0947ee1adf",
            "b50c3e3d-7bd0-4118-856f-19b26b21d01f",
            "b64f09fd-52d1-c48e-af23-4ce918e7bf3b",
            "b882b24d-1776-4ef9-9016-0bdbd935eda3",
            "b8ca5a94-8fff-27ad-6e00-96e244a32e21",
            "b9424309-f601-4a69-98ca-ca68e65633e6",
            "c411adc1-9661-4fb5-a4c1-8cfe74911943",
            "cbf9e891-d079-2b28-5617-283450d463dd",
            "d87f251c-3e12-a8bf-e2d0-afd43d36c5f9",
            "db0e1627-ae89-4c25-8944-a9481d8512d9",
            "e02f23df-8d48-2b1b-3b5c-6dd27302c61c",
            "e718e983-09aa-e8f6-b25f-cd4b395d4785",
            "e7547f62-187b-b641-d462-e54a3f813d9a",
            "eb62e768-151b-45d1-9fe5-9e1d2a5991c5",
            "f312610a-ebfb-a106-ea92-fd643c5d3636",
            "f943d7bc-872e-4a81-810f-94d26465da69",
            "fa0fc42c-d91d-fca7-34eb-806ff46062dc",
            "fca3e3ee-56cd-f474-dc31-307fd24a891d",
            "fe75ce3f-1545-400c-b28c-ad771054e69f",
            "fed4f021-ff3e-942a-010e-afa43fda2136",
        ],
        ("main", "translations-wasm") => [
            "4fd32605-9889-4dd9-9fc7-577ad1136746",
        ]
    }
}

impl<C: ApiClient> RemoteSettingsClient<C> {
    pub fn new_from_parts(
        collection_name: String,
        storage: Storage,
        jexl_filter: JexlFilter,
        api_client: C,
    ) -> Self {
        Self {
            collection_name,
            inner: Mutex::new(RemoteSettingsClientInner {
                storage,
                api_client,
                jexl_filter,
            }),
            pending_config: Mutex::new(None),
        }
    }

    /// Lock the `RemoteSettingsClientInner` field
    ///
    /// This also applies the pending config if set.
    fn lock_inner(&self) -> Result<MutexGuard<'_, RemoteSettingsClientInner<C>>> {
        let pending_config = self.get_pending_config();
        let mut inner = self.inner.lock();
        if let Some(config) = pending_config {
            inner.api_client =
                C::create(config.server_url, config.bucket_name, &self.collection_name);
            inner.jexl_filter = JexlFilter::new(config.context);
            inner.storage.empty()?;
        }
        Ok(inner)
    }

    fn get_pending_config(&self) -> Option<RemoteSettingsClientConfig> {
        self.pending_config.lock().take()
    }

    pub fn collection_name(&self) -> &str {
        &self.collection_name
    }

    fn load_packaged_timestamp(&self) -> Option<u64> {
        // Using the macro generated `get_packaged_timestamp` in macros.rs
        Self::get_packaged_timestamp(&self.collection_name)
    }

    fn load_packaged_data(&self) -> Option<CollectionData> {
        // Using the macro generated `get_packaged_data` in macros.rs
        let str_data = Self::get_packaged_data(&self.collection_name)?;
        let data: CollectionData = serde_json::from_str(str_data).ok()?;
        debug_assert_eq!(data.timestamp, self.load_packaged_timestamp().unwrap());
        Some(data)
    }

    fn load_packaged_attachment(&self, filename: &str) -> Option<(&'static [u8], &'static str)> {
        // Using the macro generated `get_packaged_attachment` in macros.rs
        Self::get_packaged_attachment(&self.collection_name, filename)
    }

    /// Filters records based on the presence and evaluation of `filter_expression`.
    fn filter_records(
        &self,
        records: Vec<RemoteSettingsRecord>,
        inner: &RemoteSettingsClientInner<C>,
    ) -> Vec<RemoteSettingsRecord> {
        records
            .into_iter()
            .filter(|record| match record.fields.get("filter_expression") {
                Some(serde_json::Value::String(filter_expr)) => {
                    inner.jexl_filter.evaluate(filter_expr).unwrap_or(false)
                }
                _ => true, // Include records without a valid filter expression by default
            })
            .collect()
    }

    /// Returns the parsed packaged data, but only if it's newer than the data we have
    /// in storage. This avoids parsing the packaged data if we won't use it.
    fn get_packaged_data_if_newer(
        &self,
        storage: &mut Storage,
        collection_url: &str,
    ) -> Result<Option<CollectionData>> {
        let packaged_ts = self.load_packaged_timestamp();
        let storage_ts = storage.get_last_modified_timestamp(collection_url)?;
        let packaged_is_newer = match (packaged_ts, storage_ts) {
            (Some(packaged_ts), Some(storage_ts)) => packaged_ts > storage_ts,
            (Some(_), None) => true, // no storage data
            (None, _) => false,      // no packaged data
        };

        if packaged_is_newer {
            Ok(self.load_packaged_data())
        } else {
            Ok(None)
        }
    }

    /// Get the current set of records.
    ///
    /// If records are not present in storage this will normally return None.  Use `sync_if_empty =
    /// true` to change this behavior and perform a network request in this case.
    pub fn get_records(&self, sync_if_empty: bool) -> Result<Option<Vec<RemoteSettingsRecord>>> {
        let mut inner = self.lock_inner()?;
        let collection_url = inner.api_client.collection_url();

        // Case 1: The packaged data is more recent than the cache
        //
        // This happens when there's no cached data or when we get new packaged data because of a
        // product update
        if inner.api_client.is_prod_server()? {
            if let Some(packaged_data) =
                self.get_packaged_data_if_newer(&mut inner.storage, &collection_url)?
            {
                // Remove previously cached data (packaged data does not have tombstones like diff responses do).
                inner.storage.empty()?;
                // Insert new packaged data.
                inner.storage.insert_collection_content(
                    &collection_url,
                    &packaged_data.data,
                    packaged_data.timestamp,
                    CollectionMetadata::default(),
                )?;
                return Ok(Some(self.filter_records(packaged_data.data, &inner)));
            }
        }

        let cached_records = inner.storage.get_records(&collection_url)?;

        match (cached_records, sync_if_empty) {
            // Case 2: We have cached records
            //
            // Note: we should return these even if it's an empty list and `sync_if_empty=true`.
            // The "if empty" part refers to the cache being empty, not the list.
            (Some(cached_records), _) => Ok(Some(self.filter_records(cached_records, &inner))),
            // Case 3: sync_if_empty=true
            (None, true) => {
                // `sync()` takes the lock, release it first.
                drop(inner);
                // Sync and verify content signatures.
                self.sync()?;
                // Return what was just stored.
                let mut inner = self.lock_inner()?;
                Ok(inner
                    .storage
                    .get_records(&collection_url)?
                    .map(|records| self.filter_records(records, &inner)))
            }
            // Case 4: Nothing to return
            (None, false) => Ok(None),
        }
    }

    /// Returns the last modified timestamp for the collection.
    pub fn get_last_modified_timestamp(&self) -> Result<Option<u64>> {
        let mut inner = self.lock_inner()?;
        let collection_url = inner.api_client.collection_url();
        inner.storage.get_last_modified_timestamp(&collection_url)
    }

    /// Synchronizes the local collection with the remote server by performing the following steps:
    /// 1. Fetches the last modified timestamp of the collection from local storage.
    /// 2. Fetches the changeset from the remote server based on the last modified timestamp.
    /// 3. Inserts the fetched changeset into local storage.
    fn perform_sync_operation(&self) -> Result<()> {
        let mut inner = self.lock_inner()?;
        let collection_url = inner.api_client.collection_url();
        let timestamp = inner.storage.get_last_modified_timestamp(&collection_url)?;
        let changeset = inner.api_client.fetch_changeset(timestamp)?;
        debug!(
            "{0}: apply {1} change(s) locally.",
            self.collection_name,
            changeset.changes.len()
        );
        inner.storage.insert_collection_content(
            &collection_url,
            &changeset.changes,
            changeset.timestamp,
            changeset.metadata,
        )
    }

    pub fn sync(&self) -> Result<()> {
        // First attempt
        self.perform_sync_operation()?;
        // Verify that inserted data has valid signature
        if self.verify_signature().is_err() {
            debug!(
                "{0}: signature verification failed. Reset and retry.",
                self.collection_name
            );
            // Retry with packaged dataset as base
            self.reset_storage()?;
            self.perform_sync_operation()?;
            // Verify signature again
            self.verify_signature().inspect_err(|_| {
                // And reset with packaged data if it fails again.
                self.reset_storage()
                    .expect("Failed to reset storage after verification failure");
            })?;
        }
        trace!("{0}: sync done.", self.collection_name);
        Ok(())
    }

    pub fn run_maintenance(&self) -> Result<()> {
        let mut inner = self.lock_inner()?;
        inner.storage.run_maintenance()
    }

    pub fn reset_storage(&self) -> Result<()> {
        trace!("{0}: reset local storage.", self.collection_name);
        let mut inner = self.lock_inner()?;
        let collection_url = inner.api_client.collection_url();
        // Clear existing storage
        inner.storage.empty()?;
        // Load packaged data only for production
        if inner.api_client.is_prod_server()? {
            if let Some(packaged_data) = self.load_packaged_data() {
                trace!("{0}: restore packaged dump.", self.collection_name);
                inner.storage.insert_collection_content(
                    &collection_url,
                    &packaged_data.data,
                    packaged_data.timestamp,
                    CollectionMetadata::default(),
                )?;
            }
        }
        Ok(())
    }

    pub fn shutdown(&self) {
        self.inner.lock().storage.close();
    }

    #[cfg(not(feature = "signatures"))]
    fn verify_signature(&self) -> Result<()> {
        debug!("{0}: signature verification skipped.", self.collection_name);
        Ok(())
    }

    #[cfg(feature = "signatures")]
    fn verify_signature(&self) -> Result<()> {
        let mut inner = self.lock_inner()?;
        let collection_url = inner.api_client.collection_url();
        let timestamp = inner.storage.get_last_modified_timestamp(&collection_url)?;
        let records = inner.storage.get_records(&collection_url)?;
        let metadata = inner.storage.get_collection_metadata(&collection_url)?;
        match (timestamp, &records, metadata) {
            (Some(timestamp), Some(records), Some(metadata)) => {
                // rc_crypto verifies that the provided certificates chain leads to our root certificate.
                let expected_root_hash = if inner.api_client.is_prod_server()? {
                    ROOT_CERT_SHA256_HASH_PROD
                } else {
                    ROOT_CERT_SHA256_HASH_NONPROD
                };
                // Iterate through the list of signatures, and verify that at least one of them is valid.
                // This allows for key rotation without breaking clients that have an old certificate chain cached.
                let mut result = Err(Error::IncompleteSignatureDataError(
                    "No valid signatures found".into(),
                ));
                for signature in &metadata.signatures {
                    if signature.mode != "p384ecdsa" {
                        // We currently only support ECDSA P384.
                        // Change this once `rc_crypto` will support more types (eg. post-quantum algorithms).
                        continue;
                    }

                    let cert_chain_bytes = inner.api_client.fetch_cert(&signature.x5u)?;

                    // The signer name is hard-coded. This would have to be modified in the very (very)
                    // unlikely situation where we would add a new collection signer.
                    // And clients code would have to be modified to handle this new collection anyway.
                    // https://searchfox.org/mozilla-central/rev/df850fa290fe962c2c5ae8b63d0943ce768e3cc4/services/settings/remote-settings.sys.mjs#40-48
                    let expected_leaf_cname = format!(
                        "{}.content-signature.mozilla.org",
                        if metadata.bucket.contains("security-state") {
                            "onecrl"
                        } else {
                            "remote-settings"
                        }
                    );

                    result = signatures::verify_signature(
                        timestamp,
                        records,
                        signature.signature.as_bytes(),
                        &cert_chain_bytes,
                        epoch_seconds(),
                        expected_root_hash,
                        &expected_leaf_cname,
                    )
                    .inspect_err(|err| {
                        debug!(
                            "{0}: bad signature ({1:?}) using certificate {2} and signer '{3}'",
                            self.collection_name, err, &signature.x5u, expected_leaf_cname
                        );
                    });
                    // If verification succeeds, then we exit!
                    if result.is_ok() {
                        trace!("{0}: signature verification success.", self.collection_name);
                        return Ok(());
                    }
                }
                // If we tried all signatures and none worked, then we return an error.
                result
            }
            _ => {
                let missing_field = if timestamp.is_none() {
                    "timestamp"
                } else if records.is_none() {
                    "records"
                } else {
                    "metadata"
                };
                Err(Error::IncompleteSignatureDataError(missing_field.into()))
            }
        }
    }

    /// Downloads an attachment from [attachment_location]. NOTE: there are no guarantees about a
    /// maximum size, so use care when fetching potentially large attachments.
    pub fn get_attachment(&self, record: &RemoteSettingsRecord) -> Result<Vec<u8>> {
        let metadata = record
            .attachment
            .as_ref()
            .ok_or_else(|| Error::RecordAttachmentMismatchError("No attachment metadata".into()))?;

        let mut inner = self.lock_inner()?;
        let collection_url = inner.api_client.collection_url();

        // First try storage - it will only return data that matches our metadata
        if let Some(data) = inner
            .storage
            .get_attachment(&collection_url, metadata.clone())?
        {
            return Ok(data);
        }

        // Then try packaged data if we're in prod
        if inner.api_client.is_prod_server()? {
            if let Some((data, manifest)) = self.load_packaged_attachment(&record.id) {
                if let Ok(manifest_data) = serde_json::from_str::<serde_json::Value>(manifest) {
                    if metadata.hash == manifest_data["hash"].as_str().unwrap_or_default()
                        && metadata.size == manifest_data["size"].as_u64().unwrap_or_default()
                    {
                        // Store valid packaged data in storage because it was either empty or outdated
                        inner
                            .storage
                            .set_attachment(&collection_url, &metadata.location, data)?;
                        return Ok(data.to_vec());
                    }
                }
            }
        }

        // Try to download the attachment because neither the storage nor the local data had it
        let attachment = inner.api_client.fetch_attachment(&metadata.location)?;

        // Verify downloaded data
        if attachment.len() as u64 != metadata.size {
            return Err(Error::RecordAttachmentMismatchError(
                "Downloaded attachment size mismatch".into(),
            ));
        }
        let hash = format!("{:x}", Sha256::digest(&attachment));
        if hash != metadata.hash {
            return Err(Error::RecordAttachmentMismatchError(
                "Downloaded attachment hash mismatch".into(),
            ));
        }

        // Store verified download in storage
        inner
            .storage
            .set_attachment(&collection_url, &metadata.location, &attachment)?;
        Ok(attachment)
    }

    pub fn update_config(
        &self,
        server_url: BaseUrl,
        bucket_name: String,
        context: Option<RemoteSettingsContext>,
    ) {
        let mut pending_config = self.pending_config.lock();
        *pending_config = Some(RemoteSettingsClientConfig {
            server_url,
            bucket_name,
            context,
        })
    }
}

impl RemoteSettingsClient<ViaductApiClient> {
    pub fn new(
        server_url: BaseUrl,
        bucket_name: String,
        collection_name: String,
        context: Option<RemoteSettingsContext>,
        storage: Storage,
    ) -> Self {
        let api_client = ViaductApiClient::new(server_url, &bucket_name, &collection_name);
        let jexl_filter = JexlFilter::new(context);

        Self::new_from_parts(collection_name, storage, jexl_filter, api_client)
    }
}

#[cfg_attr(test, mockall::automock)]
pub trait ApiClient {
    /// Create a new instance of the client
    fn create(server_url: BaseUrl, bucket_name: String, collection_name: &str) -> Self;

    /// Get the Bucket URL for this client.
    ///
    /// This is a URL that includes the server URL, bucket name, and collection name.  This is used
    /// to check if the application has switched the remote settings config and therefore we should
    /// throw away any cached data
    ///
    /// Returns it as a String, since that's what the storage expects
    fn collection_url(&self) -> String;

    /// Fetch records from the server
    fn fetch_changeset(&mut self, timestamp: Option<u64>) -> Result<ChangesetResponse>;

    /// Fetch an attachment from the server
    fn fetch_attachment(&mut self, attachment_location: &str) -> Result<Vec<u8>>;

    /// Fetch a server certificate
    fn fetch_cert(&mut self, x5u: &str) -> Result<Vec<u8>>;

    /// Check if this client is pointing to the production server
    fn is_prod_server(&self) -> Result<bool>;
}

/// Client for Remote settings API requests
pub struct ViaductApiClient {
    endpoints: RemoteSettingsEndpoints,
    remote_state: RemoteState,
}

impl ViaductApiClient {
    fn new(base_url: BaseUrl, bucket_name: &str, collection_name: &str) -> Self {
        Self {
            endpoints: RemoteSettingsEndpoints::new(&base_url, bucket_name, collection_name),
            remote_state: RemoteState::default(),
        }
    }

    fn make_request(&mut self, url: Url) -> Result<Response> {
        self._make_request(url.clone())
            .inspect_err(|e| breadcrumb!("Request error: {e} ({url})"))
    }

    fn _make_request(&mut self, url: Url) -> Result<Response> {
        self.remote_state.ensure_no_backoff()?;

        let req = Request::get(url);
        let resp = req.send()?;

        self.remote_state.handle_backoff_hint(&resp)?;

        if resp.is_success() {
            Ok(resp)
        } else {
            Err(Error::response_error(&resp))
        }
    }
}

impl ApiClient for ViaductApiClient {
    fn create(server_url: BaseUrl, bucket_name: String, collection_name: &str) -> Self {
        Self::new(server_url, &bucket_name, collection_name)
    }

    fn collection_url(&self) -> String {
        self.endpoints.collection_url.to_string()
    }

    fn fetch_changeset(&mut self, timestamp: Option<u64>) -> Result<ChangesetResponse> {
        let mut url = self.endpoints.changeset_url.clone();
        // 0 is used as an arbitrary value for `_expected` because the current implementation does
        // not leverage push timestamps or polling from the monitor/changes endpoint. More
        // details:
        //
        // https://remote-settings.readthedocs.io/en/latest/client-specifications.html#cache-busting
        url.query_pairs_mut().append_pair("_expected", "0");
        if let Some(timestamp) = timestamp {
            url.query_pairs_mut()
                .append_pair("_since", &format!("{}", timestamp));
        }

        let resp = self.make_request(url)?;

        if resp.is_success() {
            Ok(resp.json::<ChangesetResponse>()?)
        } else {
            Err(Error::response_error(&resp))
        }
    }

    fn fetch_attachment(&mut self, attachment_location: &str) -> Result<Vec<u8>> {
        let attachments_base_url = match &self.remote_state.attachments_base_url {
            Some(attachments_base_url) => attachments_base_url.to_owned(),
            None => {
                let server_info = self
                    .make_request(self.endpoints.root_url.clone())?
                    .json::<ServerInfo>()?;
                let attachments_base_url = match server_info.capabilities.attachments {
                    Some(capability) => Url::parse(&capability.base_url)?,
                    None => Err(Error::AttachmentsUnsupportedError)?,
                };
                self.remote_state.attachments_base_url = Some(attachments_base_url.clone());
                attachments_base_url
            }
        };

        let resp = self.make_request(attachments_base_url.join(attachment_location)?)?;
        Ok(resp.body)
    }

    fn is_prod_server(&self) -> Result<bool> {
        Ok(self
            .endpoints
            .root_url
            .as_str()
            .starts_with(RemoteSettingsServer::Prod.get_url()?.as_str()))
    }

    fn fetch_cert(&mut self, x5u: &str) -> Result<Vec<u8>> {
        let resp = self
            .make_request(Url::parse(x5u)?)
            .map_err(|err| Error::CertificateError(Box::new(err)))?;
        let content_type = resp
            .headers
            .get(viaduct::header_names::CONTENT_TYPE)
            .unwrap_or_default();
        if !content_type.contains("text/plain") {
            return Err(Error::CertificateError(Box::new(Error::ResponseError {
                url: resp.url.to_string(),
                message: format!("unexpected content-type: {content_type:?}"),
            })));
        }
        Ok(resp.body)
    }
}

/// Stores all the endpoints for a Remote Settings server
///
/// There's actually not to many of these, so we can just pack them all into a struct
struct RemoteSettingsEndpoints {
    /// Root URL for Remote Settings server
    ///
    /// This has the form `[base-url]/`. It's where we get the attachment base url from.
    root_url: Url,
    /// URL for the collections endpoint
    ///
    /// This has the form:
    /// `[base-url]/buckets/[bucket-name]/collections/[collection-name]`.
    ///
    /// It can be used to fetch some metadata about the collection, but the real reason we use it
    /// is to get a URL that uniquely identifies the server + bucket name.  This is used by the
    /// [Storage] component to know when to throw away cached records because the user has changed
    /// one of these,
    collection_url: Url,
    /// URL for the changeset request
    ///
    /// This has the form:
    /// `[base-url]/buckets/[bucket-name]/collections/[collection-name]/changeset`.
    ///
    /// This is the URL for fetching records and changes to records
    changeset_url: Url,
}

impl RemoteSettingsEndpoints {
    /// Construct a new RemoteSettingsEndpoints
    ///
    /// `base_url` should have the form `https://[domain]/v2` (no trailing slash).
    fn new(base_url: &BaseUrl, bucket_name: &str, collection_name: &str) -> Self {
        let mut root_url = base_url.clone();
        // Push the empty string to add the trailing slash.
        root_url.path_segments_mut().push("");

        let mut collection_url = base_url.clone();
        collection_url
            .path_segments_mut()
            .push("buckets")
            .push(bucket_name)
            .push("collections")
            .push(collection_name);

        let mut changeset_url = collection_url.clone();
        changeset_url.path_segments_mut().push("changeset");

        Self {
            root_url: root_url.into_inner(),
            collection_url: collection_url.into_inner(),
            changeset_url: changeset_url.into_inner(),
        }
    }
}

#[derive(Clone, Deserialize, Serialize)]
pub struct ChangesetResponse {
    changes: Vec<RemoteSettingsRecord>,
    timestamp: u64,
    metadata: CollectionMetadata,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize, Eq, PartialEq)]
pub struct CollectionMetadata {
    pub bucket: String,
    pub signatures: Vec<CollectionSignature>,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize, Eq, PartialEq)]
pub struct CollectionSignature {
    pub signature: String,
    /// X.509 certificate chain Url (x5u)
    pub x5u: String,
    /// Signature type
    pub mode: String,
}

/// A parsed Remote Settings record. Records can contain arbitrary fields, so clients
/// are required to further extract expected values from the [fields] member.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq, uniffi::Record)]
pub struct RemoteSettingsRecord {
    pub id: String,
    pub last_modified: u64,
    /// Tombstone flag (see https://remote-settings.readthedocs.io/en/latest/client-specifications.html#local-state)
    #[serde(default)]
    pub deleted: bool,
    pub attachment: Option<Attachment>,
    #[serde(flatten)]
    pub fields: RsJsonObject,
}

/// Attachment metadata that can be optionally attached to a [Record]. The [location] should
/// included in calls to [Client::get_attachment].
#[derive(Clone, Debug, Default, Deserialize, Serialize, Eq, PartialEq, uniffi::Record)]
pub struct Attachment {
    pub filename: String,
    pub mimetype: String,
    pub location: String,
    pub hash: String,
    pub size: u64,
}

// Define a UniFFI custom types to pass JSON objects across the FFI as a string
//
// This is named `RsJsonObject` because, UniFFI cannot currently rename iOS bindings and JsonObject
// conflicted with the declaration in Nimbus. This shouldn't really impact Android, since the type
// is converted into the platform JsonObject thanks to the UniFFI binding.
pub type RsJsonObject = serde_json::Map<String, serde_json::Value>;
uniffi::custom_type!(RsJsonObject, String, {
    remote,
    try_lift: |val| {
        let json: serde_json::Value = serde_json::from_str(&val)?;

        match json {
            serde_json::Value::Object(obj) => Ok(obj),
            _ => Err(uniffi::deps::anyhow::anyhow!(
                "Unexpected JSON-non-object in the bagging area"
            )),
        }
    },
    lower: |obj| serde_json::Value::Object(obj).to_string(),
});

#[derive(Clone, Debug)]
pub(crate) struct RemoteState {
    attachments_base_url: Option<Url>,
    backoff: BackoffState,
}

impl Default for RemoteState {
    fn default() -> Self {
        Self {
            attachments_base_url: None,
            backoff: BackoffState::Ok,
        }
    }
}

impl RemoteState {
    pub fn handle_backoff_hint(&mut self, response: &Response) -> Result<()> {
        let extract_backoff_header = |header| -> Result<u64> {
            Ok(response
                .headers
                .get_as::<u64, _>(header)
                .transpose()
                .unwrap_or_default() // Ignore number parsing errors.
                .unwrap_or(0))
        };
        // In practice these two headers are mutually exclusive.
        let backoff = extract_backoff_header(HEADER_BACKOFF)?;
        let retry_after = extract_backoff_header(HEADER_RETRY_AFTER)?;
        let max_backoff = backoff.max(retry_after);

        if max_backoff > 0 {
            self.backoff = BackoffState::Backoff {
                observed_at: Instant::now(),
                duration: Duration::from_secs(max_backoff),
            };
        }
        Ok(())
    }

    pub fn ensure_no_backoff(&mut self) -> Result<()> {
        if let BackoffState::Backoff {
            observed_at,
            duration,
        } = self.backoff
        {
            let elapsed_time = observed_at.elapsed();
            if elapsed_time >= duration {
                self.backoff = BackoffState::Ok;
            } else {
                let remaining = duration - elapsed_time;
                return Err(Error::BackoffError(remaining.as_secs()));
            }
        }
        Ok(())
    }
}

/// Used in handling backoff responses from the Remote Settings server.
#[derive(Clone, Copy, Debug)]
pub(crate) enum BackoffState {
    Ok,
    Backoff {
        observed_at: Instant,
        duration: Duration,
    },
}

#[derive(Deserialize)]
struct ServerInfo {
    capabilities: Capabilities,
}

#[derive(Deserialize)]
struct Capabilities {
    attachments: Option<AttachmentsCapability>,
}

#[derive(Deserialize)]
struct AttachmentsCapability {
    base_url: String,
}

#[cfg(test)]
mod test_new_client {
    use super::*;

    #[test]
    fn test_endpoints() {
        let endpoints = RemoteSettingsEndpoints::new(
            &BaseUrl::parse("http://rs.example.com/v2").unwrap(),
            "main",
            "test-collection",
        );
        assert_eq!(endpoints.root_url.to_string(), "http://rs.example.com/v2/");
        assert_eq!(
            endpoints.collection_url.to_string(),
            "http://rs.example.com/v2/buckets/main/collections/test-collection",
        );
        assert_eq!(
            endpoints.changeset_url.to_string(),
            "http://rs.example.com/v2/buckets/main/collections/test-collection/changeset",
        );
    }
}

#[cfg(test)]
mod viaduct_client_tests {
    use super::*;

    #[test]
    fn test_fetch_uses_local_timestamp_as_unquoted_since() {
        viaduct_dev::init_backend_dev();
        let changeset = mockito::mock(
            "GET",
            "/v2/buckets/main/collections/test-collection/changeset",
        )
        // The mock only matches if `_since` is sent unquoted.
        .match_query(mockito::Matcher::AllOf(vec![
            mockito::Matcher::UrlEncoded("_expected".into(), "0".into()),
            mockito::Matcher::UrlEncoded("_since".into(), "42".into()),
        ]))
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_body(
            r#"{"changes": [], "timestamp": 42, "metadata": {"bucket": "main", "signatures": []}}"#,
        )
        .create();

        let mut api_client = ViaductApiClient::new(
            BaseUrl::parse(&format!("{}/v2", mockito::server_url())).unwrap(),
            "main",
            "test-collection",
        );
        api_client.fetch_changeset(Some(42)).unwrap();

        changeset.assert();
    }

    fn api_client() -> ViaductApiClient {
        ViaductApiClient::new(
            BaseUrl::parse(&format!("{}/v2", mockito::server_url())).unwrap(),
            "main",
            "test-collection",
        )
    }

    #[test]
    fn test_fetch_cert_success() {
        viaduct_dev::init_backend_dev();
        let _m = mockito::mock("GET", "/chains/ok.pem")
            .with_status(200)
            .with_header("content-type", "text/plain; charset=utf-8")
            .with_body("PEM")
            .create();
        let x5u = format!("{}/chains/ok.pem", mockito::server_url());
        assert_eq!(api_client().fetch_cert(&x5u).unwrap(), b"PEM");
    }

    #[test]
    fn test_fetch_cert_not_found() {
        viaduct_dev::init_backend_dev();
        let _m = mockito::mock("GET", "/chains/missing.pem")
            .with_status(404)
            .create();
        let x5u = format!("{}/chains/missing.pem", mockito::server_url());
        let err = api_client().fetch_cert(&x5u).unwrap_err();
        assert!(matches!(err, Error::CertificateError(_)), "{err:?}");
    }

    #[test]
    fn test_fetch_cert_bad_content_type() {
        viaduct_dev::init_backend_dev();
        let _m = mockito::mock("GET", "/chains/html.pem")
            .with_status(200)
            .with_header("content-type", "text/html")
            .with_body("<html></html>")
            .create();
        let x5u = format!("{}/chains/html.pem", mockito::server_url());
        let err = api_client().fetch_cert(&x5u).unwrap_err();
        assert!(matches!(err, Error::CertificateError(_)), "{err:?}");
    }

    #[test]
    fn test_fetch_cert_server_error() {
        viaduct_dev::init_backend_dev();
        let _m = mockito::mock("GET", "/chains/broken.pem")
            .with_status(500)
            .create();
        let x5u = format!("{}/chains/broken.pem", mockito::server_url());
        let err = api_client().fetch_cert(&x5u).unwrap_err();
        assert!(matches!(err, Error::CertificateError(_)), "{err:?}");
    }
}

#[cfg(test)]
mod jexl_tests {
    use super::*;
    use std::sync::{Arc, Weak};

    #[test]
    fn test_get_records_filtered_app_version_pass() {
        let mut api_client = MockApiClient::new();
        let records = vec![RemoteSettingsRecord {
            id: "record-0001".into(),
            last_modified: 100,
            deleted: false,
            attachment: None,
            fields: serde_json::json!({
                "filter_expression": "env.version|versionCompare(\"128.0a1\") > 0"
            })
            .as_object()
            .unwrap()
            .clone(),
        }];
        let changeset = ChangesetResponse {
            changes: records.clone(),
            timestamp: 42,
            metadata: CollectionMetadata::default(),
        };
        api_client.expect_collection_url().returning(|| {
            "http://rs.example.com/v2/buckets/main/collections/test-collection".into()
        });
        api_client.expect_fetch_changeset().returning({
            let changeset = changeset.clone();
            move |timestamp| {
                assert_eq!(timestamp, None);
                Ok(changeset.clone())
            }
        });
        api_client.expect_is_prod_server().returning(|| Ok(false));

        let context = RemoteSettingsContext {
            app_version: Some("129.0.0".to_string()),
            ..Default::default()
        };

        let mut storage = Storage::new(":memory:".into());
        let _ = storage.insert_collection_content(
            "http://rs.example.com/v2/buckets/main/collections/test-collection",
            &records,
            42,
            CollectionMetadata::default(),
        );

        let rs_client = RemoteSettingsClient::new_from_parts(
            "test-collection".into(),
            storage,
            JexlFilter::new(Some(context)),
            api_client,
        );

        assert_eq!(
            rs_client.get_records(false).expect("Error getting records"),
            Some(records)
        );
    }

    #[test]
    fn test_get_records_filtered_app_version_too_low() {
        let mut api_client = MockApiClient::new();
        let records = vec![RemoteSettingsRecord {
            id: "record-0001".into(),
            last_modified: 100,
            deleted: false,
            attachment: None,
            fields: serde_json::json!({
                "filter_expression": "env.version|versionCompare(\"128.0a1\") > 0"
            })
            .as_object()
            .unwrap()
            .clone(),
        }];
        let changeset = ChangesetResponse {
            changes: records.clone(),
            timestamp: 42,
            metadata: CollectionMetadata::default(),
        };
        api_client.expect_collection_url().returning(|| {
            "http://rs.example.com/v2/buckets/main/collections/test-collection".into()
        });
        api_client.expect_fetch_changeset().returning({
            let changeset = changeset.clone();
            move |timestamp| {
                assert_eq!(timestamp, None);
                Ok(changeset.clone())
            }
        });
        api_client.expect_is_prod_server().returning(|| Ok(false));

        let context = RemoteSettingsContext {
            app_version: Some("127.0.0.".to_string()),
            ..Default::default()
        };

        let mut storage = Storage::new(":memory:".into());
        let _ = storage.insert_collection_content(
            "http://rs.example.com/v2/buckets/main/collections/test-collection",
            &records,
            42,
            CollectionMetadata::default(),
        );

        let rs_client = RemoteSettingsClient::new_from_parts(
            "test-collection".into(),
            storage,
            JexlFilter::new(Some(context)),
            api_client,
        );

        assert_eq!(
            rs_client.get_records(false).expect("Error getting records"),
            Some(vec![])
        );
    }

    #[test]
    fn test_update_jexl_context() {
        let mut api_client = MockApiClient::new();
        let records = vec![RemoteSettingsRecord {
            id: "record-0001".into(),
            last_modified: 100,
            deleted: false,
            attachment: None,
            fields: serde_json::json!({
                "filter_expression": "env.country == \"US\""
            })
            .as_object()
            .unwrap()
            .clone(),
        }];
        let changeset = ChangesetResponse {
            changes: records.clone(),
            timestamp: 42,
            metadata: CollectionMetadata::default(),
        };
        api_client.expect_collection_url().returning(|| {
            "http://rs.example.com/v2/buckets/main/collections/test-collection".into()
        });
        api_client.expect_fetch_changeset().returning({
            let changeset = changeset.clone();
            move |timestamp| {
                assert_eq!(timestamp, None);
                Ok(changeset.clone())
            }
        });
        api_client.expect_is_prod_server().returning(|| Ok(false));

        let context = RemoteSettingsContext {
            country: Some("US".to_string()),
            ..Default::default()
        };

        let mut storage = Storage::new(":memory:".into());
        let _ = storage.insert_collection_content(
            "http://rs.example.com/v2/buckets/main/collections/test-collection",
            &records,
            42,
            CollectionMetadata::default(),
        );

        let rs_client = RemoteSettingsClient::new_from_parts(
            "test-collection".into(),
            storage,
            JexlFilter::new(Some(context)),
            api_client,
        );

        assert_eq!(
            rs_client.get_records(false).expect("Error getting records"),
            Some(records)
        );

        // We can't call `update_config` directly, since that only works with a real API client.
        // Instead, just execute the code from that method that updates the JEXL filter.
        rs_client.inner.lock().jexl_filter = JexlFilter::new(Some(RemoteSettingsContext {
            country: Some("UK".to_string()),
            ..Default::default()
        }));

        assert_eq!(
            rs_client.get_records(false).expect("Error getting records"),
            Some(vec![])
        );
    }

    #[test]
    fn test_jexl_filter_by_distribution() {
        let mut api_client = MockApiClient::new();
        let records = vec![RemoteSettingsRecord {
            id: "record-0001".into(),
            last_modified: 100,
            deleted: false,
            attachment: None,
            fields: serde_json::json!({
                "filter_expression": "env.distribution == \"firefox-001\""
            })
            .as_object()
            .unwrap()
            .clone(),
        }];
        let changeset = ChangesetResponse {
            changes: records.clone(),
            timestamp: 42,
            metadata: CollectionMetadata::default(),
        };
        api_client.expect_collection_url().returning(|| {
            "http://rs.example.com/v2/buckets/main/collections/test-collection".into()
        });
        api_client.expect_fetch_changeset().returning({
            let changeset = changeset.clone();
            move |timestamp| {
                assert_eq!(timestamp, None);
                Ok(changeset.clone())
            }
        });
        api_client.expect_is_prod_server().returning(|| Ok(false));

        let context = RemoteSettingsContext {
            distribution: Some("firefox-001".to_string()),
            ..Default::default()
        };

        let mut storage = Storage::new(":memory:".into());
        let _ = storage.insert_collection_content(
            "http://rs.example.com/v2/buckets/main/collections/test-collection",
            &records,
            42,
            CollectionMetadata::default(),
        );

        let rs_client = RemoteSettingsClient::new_from_parts(
            "test-collection".into(),
            storage,
            JexlFilter::new(Some(context)),
            api_client,
        );

        assert_eq!(
            rs_client.get_records(false).expect("Error getting records"),
            Some(records)
        );

        rs_client.inner.lock().jexl_filter = JexlFilter::new(Some(RemoteSettingsContext {
            distribution: Some("default".to_string()),
            ..Default::default()
        }));

        assert_eq!(
            rs_client.get_records(false).expect("Error getting records"),
            Some(vec![])
        );
    }

    // Test that we can't hit the deadlock described in
    // https://bugzilla.mozilla.org/show_bug.cgi?id=2012955
    #[test]
    fn test_update_config_deadlock() {
        let mut api_client = MockApiClient::new();
        let rs_client_ref: Arc<Mutex<Weak<RemoteSettingsClient<MockApiClient>>>> =
            Arc::new(Mutex::new(Weak::new()));
        let rs_client_ref2 = rs_client_ref.clone();

        api_client.expect_collection_url().returning(move || {
            // While we're in the middle of `get_records()` and have the `RemoteSettingsClientInner`
            // locked, call `update_config` to try to trigger the deadlock.
            //
            // Note: this code path is impossible in practice, since the client never calls
            // `update_config` in the middle of `get_records()`. What happens on desktop is that
            // `get_records()` needs to execute some Necko code in the main thread, while
            // `update_config` is also running in the main thread and blocked getting the lock.
            //
            // The two scenarios are different, but if this one doesn't deadlock then the real-life
            // Desktop scenario won't either.
            rs_client_ref2
                .lock()
                .upgrade()
                .expect("rs_client_ref not set")
                .update_config(
                    BaseUrl::parse("https://example.com/").unwrap(),
                    "test-collection".to_string(),
                    None,
                );
            "http://rs.example.com/v2/buckets/main/collections/test-collection".into()
        });
        api_client.expect_is_prod_server().returning(|| Ok(false));

        let context = RemoteSettingsContext {
            app_version: Some("129.0.0".to_string()),
            ..Default::default()
        };
        let storage = Storage::new(":memory:".into());

        let rs_client = Arc::new(RemoteSettingsClient::new_from_parts(
            "test-collection".into(),
            storage,
            JexlFilter::new(Some(context)),
            api_client,
        ));
        *rs_client_ref.lock() = Arc::downgrade(&rs_client);

        assert_eq!(
            rs_client.get_records(false).expect("Error getting records"),
            None,
        );
    }
}

#[cfg(feature = "signatures")]
#[cfg(test)]
mod test_signatures {
    use core::assert_eq;

    use crate::RemoteSettingsContext;

    use super::*;
    use nss_as::ensure_initialized;

    const VALID_CERTIFICATE: &str = "\
-----BEGIN CERTIFICATE-----
MIIDBjCCAougAwIBAgIIFml6g0ldRGowCgYIKoZIzj0EAwMwgaMxCzAJBgNVBAYT
AlVTMRwwGgYDVQQKExNNb3ppbGxhIENvcnBvcmF0aW9uMS8wLQYDVQQLEyZNb3pp
bGxhIEFNTyBQcm9kdWN0aW9uIFNpZ25pbmcgU2VydmljZTFFMEMGA1UEAww8Q29u
dGVudCBTaWduaW5nIEludGVybWVkaWF0ZS9lbWFpbEFkZHJlc3M9Zm94c2VjQG1v
emlsbGEuY29tMB4XDTIxMDIwMzE1MDQwNVoXDTIxMDQyNDE1MDQwNVowgakxCzAJ
BgNVBAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9ybmlhMRYwFAYDVQQHEw1Nb3VudGFp
biBWaWV3MRwwGgYDVQQKExNNb3ppbGxhIENvcnBvcmF0aW9uMRcwFQYDVQQLEw5D
bG91ZCBTZXJ2aWNlczE2MDQGA1UEAxMtcmVtb3RlLXNldHRpbmdzLmNvbnRlbnQt
c2lnbmF0dXJlLm1vemlsbGEub3JnMHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE8pKb
HX4IiD0SCy+NO7gwKqRRZ8IhGd8PTaIHIBgM6RDLRyDeswXgV+2kGUoHyzkbNKZt
zlrS3AhqeUCtl1g6ECqSmZBbRTjCpn/UCpCnMLL0T0goxtAB8Rmi3CdM0cBUo4GD
MIGAMA4GA1UdDwEB/wQEAwIHgDATBgNVHSUEDDAKBggrBgEFBQcDAzAfBgNVHSME
GDAWgBQlZawrqt0eUz/t6OdN45oKfmzy6DA4BgNVHREEMTAvgi1yZW1vdGUtc2V0
dGluZ3MuY29udGVudC1zaWduYXR1cmUubW96aWxsYS5vcmcwCgYIKoZIzj0EAwMD
aQAwZgIxAPh43Bxl4MxPT6Ra1XvboN5O2OvIn2r8rHvZPWR/jJ9vcTwH9X3F0aLJ
9FiresnsLAIxAOoAcREYB24gFBeWxbiiXaG7TR/yM1/MXw4qxbN965FFUaoB+5Bc
fS8//SQGTlCqKQ==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIF2jCCA8KgAwIBAgIEAQAAADANBgkqhkiG9w0BAQsFADCBqTELMAkGA1UEBhMC
VVMxCzAJBgNVBAgTAkNBMRYwFAYDVQQHEw1Nb3VudGFpbiBWaWV3MRwwGgYDVQQK
ExNBZGRvbnMgVGVzdCBTaWduaW5nMSQwIgYDVQQDExt0ZXN0LmFkZG9ucy5zaWdu
aW5nLnJvb3QuY2ExMTAvBgkqhkiG9w0BCQEWInNlY29wcytzdGFnZXJvb3RhZGRv
bnNAbW96aWxsYS5jb20wHhcNMjEwMTExMDAwMDAwWhcNMjQxMTE0MjA0ODU5WjCB
ozELMAkGA1UEBhMCVVMxHDAaBgNVBAoTE01vemlsbGEgQ29ycG9yYXRpb24xLzAt
BgNVBAsTJk1vemlsbGEgQU1PIFByb2R1Y3Rpb24gU2lnbmluZyBTZXJ2aWNlMUUw
QwYDVQQDDDxDb250ZW50IFNpZ25pbmcgSW50ZXJtZWRpYXRlL2VtYWlsQWRkcmVz
cz1mb3hzZWNAbW96aWxsYS5jb20wdjAQBgcqhkjOPQIBBgUrgQQAIgNiAARw1dyE
xV5aNiHJPa/fVHO6kxJn3oZLVotJ0DzFZA9r1sQf8i0+v78Pg0/c3nTAyZWfkULz
vOpKYK/GEGBtisxCkDJ+F3NuLPpSIg3fX25pH0LE15fvASBVcr8tKLVHeOmjggG6
MIIBtjAMBgNVHRMEBTADAQH/MA4GA1UdDwEB/wQEAwIBBjAWBgNVHSUBAf8EDDAK
BggrBgEFBQcDAzAdBgNVHQ4EFgQUJWWsK6rdHlM/7ejnTeOaCn5s8ugwgdkGA1Ud
IwSB0TCBzoAUhtg0HE5Y0RNcmV/YQpjtFA8Z8l2hga+kgawwgakxCzAJBgNVBAYT
AlVTMQswCQYDVQQIEwJDQTEWMBQGA1UEBxMNTW91bnRhaW4gVmlldzEcMBoGA1UE
ChMTQWRkb25zIFRlc3QgU2lnbmluZzEkMCIGA1UEAxMbdGVzdC5hZGRvbnMuc2ln
bmluZy5yb290LmNhMTEwLwYJKoZIhvcNAQkBFiJzZWNvcHMrc3RhZ2Vyb290YWRk
b25zQG1vemlsbGEuY29tggRgJZg7MDMGCWCGSAGG+EIBBAQmFiRodHRwOi8vYWRk
b25zLmFsbGl6b20ub3JnL2NhL2NybC5wZW0wTgYDVR0eBEcwRaBDMCCCHi5jb250
ZW50LXNpZ25hdHVyZS5tb3ppbGxhLm9yZzAfgh1jb250ZW50LXNpZ25hdHVyZS5t
b3ppbGxhLm9yZzANBgkqhkiG9w0BAQsFAAOCAgEAtGTTzcPzpcdf07kIeRs9vPMx
qiF8ylW5L/IQ2NzT3sFFAvPW1vW1wZC0xAHMsuVyo+BTGrv+4mlD0AUR9acRfiTZ
9qyZ3sJbyhQwJAXLKU4YpnzuFOf58T/yOnOdwpH2ky/0FuHskMyfXaAz2Az4JXJH
TCgggqfdZNvsZ5eOnQlKoC5NadMa8oTI5sd4SyR5ANUPAtYok931MvVSz3IMbwTr
v4PPWXdl9SGXuOknSqdY6/bS1LGvC2KprsT+PBlvVtS6YgZOH0uCgTTLpnrco87O
ErzC2PJBA1Ftn3Mbaou6xy7O+YX+reJ6soNUV+0JHOuKj0aTXv0c+lXEAh4Y8nea
UGhW6+MRGYMOP2NuKv8s2+CtNH7asPq3KuTQpM5RerjdouHMIedX7wpNlNk0CYbg
VMJLxZfAdwcingLWda/H3j7PxMoAm0N+eA24TGDQPC652ZakYk4MQL/45lm0A5f0
xLGKEe6JMZcTBQyO7ANWcrpVjKMiwot6bY6S2xU17mf/h7J32JXZJ23OPOKpMS8d
mljj4nkdoYDT35zFuS1z+5q6R5flLca35vRHzC3XA0H/XJvgOKUNLEW/IiJIqLNi
ab3Ao0RubuX+CAdFML5HaJmkyuJvL3YtwIOwe93RGcGRZSKZsnMS+uY5QN8+qKQz
LC4GzWQGSCGDyD+JCVw=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIHbDCCBVSgAwIBAgIEYCWYOzANBgkqhkiG9w0BAQwFADCBqTELMAkGA1UEBhMC
VVMxCzAJBgNVBAgTAkNBMRYwFAYDVQQHEw1Nb3VudGFpbiBWaWV3MRwwGgYDVQQK
ExNBZGRvbnMgVGVzdCBTaWduaW5nMSQwIgYDVQQDExt0ZXN0LmFkZG9ucy5zaWdu
aW5nLnJvb3QuY2ExMTAvBgkqhkiG9w0BCQEWInNlY29wcytzdGFnZXJvb3RhZGRv
bnNAbW96aWxsYS5jb20wHhcNMjEwMjExMjA0ODU5WhcNMjQxMTE0MjA0ODU5WjCB
qTELMAkGA1UEBhMCVVMxCzAJBgNVBAgTAkNBMRYwFAYDVQQHEw1Nb3VudGFpbiBW
aWV3MRwwGgYDVQQKExNBZGRvbnMgVGVzdCBTaWduaW5nMSQwIgYDVQQDExt0ZXN0
LmFkZG9ucy5zaWduaW5nLnJvb3QuY2ExMTAvBgkqhkiG9w0BCQEWInNlY29wcytz
dGFnZXJvb3RhZGRvbnNAbW96aWxsYS5jb20wggIiMA0GCSqGSIb3DQEBAQUAA4IC
DwAwggIKAoICAQDKRVty/FRsO4Ech6EYleyaKgAueaLYfMSsAIyPC/N8n/P8QcH8
rjoiMJrKHRlqiJmMBSmjUZVzZAP0XJku0orLKWPKq7cATt+xhGY/RJtOzenMMsr5
eN02V3GzUd1jOShUpERjzXdaO3pnfZqhdqNYqP9ocqQpyno7bZ3FZQ2vei+bF52k
51uPioTZo+1zduoR/rT01twGtZm3QpcwU4mO74ysyxxgqEy3kpojq8Nt6haDwzrj
khV9M6DGPLHZD71QaUiz5lOhD9CS8x0uqXhBhwMUBBkHsUDSxbN4ZhjDDWpCmwaD
OtbJMUJxDGPCr9qj49QESccb367OeXLrfZ2Ntu/US2Bw9EDfhyNsXr9dg9NHj5yf
4sDUqBHG0W8zaUvJx5T2Ivwtno1YZLyJwQW5pWeWn8bEmpQKD2KS/3y2UjlDg+YM
NdNASjFe0fh6I5NCFYmFWA73DpDGlUx0BtQQU/eZQJ+oLOTLzp8d3dvenTBVnKF+
uwEmoNfZwc4TTWJOhLgwxA4uK+Paaqo4Ap2RGS2ZmVkPxmroB3gL5n3k3QEXvULh
7v8Psk4+MuNWnxudrPkN38MGJo7ju7gDOO8h1jLD4tdfuAqbtQLduLXzT4DJPA4y
JBTFIRMIpMqP9CovaS8VPtMFLTrYlFh9UnEGpCeLPanJr+VEj7ae5sc8YwIDAQAB
o4IBmDCCAZQwDAYDVR0TBAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwFgYDVR0lAQH/
BAwwCgYIKwYBBQUHAwMwLAYJYIZIAYb4QgENBB8WHU9wZW5TU0wgR2VuZXJhdGVk
IENlcnRpZmljYXRlMDMGCWCGSAGG+EIBBAQmFiRodHRwOi8vYWRkb25zLm1vemls
bGEub3JnL2NhL2NybC5wZW0wHQYDVR0OBBYEFIbYNBxOWNETXJlf2EKY7RQPGfJd
MIHZBgNVHSMEgdEwgc6AFIbYNBxOWNETXJlf2EKY7RQPGfJdoYGvpIGsMIGpMQsw
CQYDVQQGEwJVUzELMAkGA1UECBMCQ0ExFjAUBgNVBAcTDU1vdW50YWluIFZpZXcx
HDAaBgNVBAoTE0FkZG9ucyBUZXN0IFNpZ25pbmcxJDAiBgNVBAMTG3Rlc3QuYWRk
b25zLnNpZ25pbmcucm9vdC5jYTExMC8GCSqGSIb3DQEJARYic2Vjb3BzK3N0YWdl
cm9vdGFkZG9uc0Btb3ppbGxhLmNvbYIEYCWYOzANBgkqhkiG9w0BAQwFAAOCAgEA
nowyJv8UaIV7NA0B3wkWratq6FgA1s/PzetG/ZKZDIW5YtfUvvyy72HDAwgKbtap
Eog6zGI4L86K0UGUAC32fBjE5lWYEgsxNM5VWlQjbgTG0dc3dYiufxfDFeMbAPmD
DzpIgN3jHW2uRqa/MJ+egHhv7kGFL68uVLboqk/qHr+SOCc1LNeSMCuQqvHwwM0+
AU1GxhzBWDkealTS34FpVxF4sT5sKLODdIS5HXJr2COHHfYkw2SW/Sfpt6fsOwaF
2iiDaK4LPWHWhhIYa6yaynJ+6O6KPlpvKYCChaTOVdc+ikyeiSO6AakJykr5Gy7d
PkkK7MDCxuY6psHj7iJQ59YK7ujQB8QYdzuXBuLLo5hc5gBcq3PJs0fLT2YFcQHA
dj+olGaDn38T0WI8ycWaFhQfKwATeLWfiQepr8JfoNlC2vvSDzGUGfdAfZfsJJZ8
5xZxahHoTFGS0mDRfXqzKH5uD578GgjOZp0fULmzkcjWsgzdpDhadGjExRZFKlAy
iKv8cXTONrGY0fyBDKennuX0uAca3V0Qm6v2VRp+7wG/pywWwc5n+04qgxTQPxgO
6pPB9UUsNbaLMDR5QPYAWrNhqJ7B07XqIYJZSwGP5xB9NqUZLF4z+AOMYgWtDpmg
IKdcFKAt3fFrpyMhlfIKkLfmm0iDjmfmIXbDGBJw9SE=
-----END CERTIFICATE-----";
    const VALID_SIGNATURE: &str = r#"fJJcOpwdnkjEWFeHXfdOJN6GaGLuDTPGzQOxA2jn6ldIleIk6KqMhZcy2GZv2uYiGwl6DERWwpaoUfQFLyCAOcVjck1qlaaEFZGY1BQba9p99xEc9FNQ3YPPfvSSZqsw"#;
    const VALID_CERT_EPOCH_SECONDS: u64 = 1615559719;

    fn build_client(
        diff_records: &[RemoteSettingsRecord],
        full_records: &[RemoteSettingsRecord],
        certificate: &str,
        signatures: &[CollectionSignature],
        epoch_secs: u64,
        bucket: &str,
    ) -> RemoteSettingsClient<MockApiClient> {
        let collection_name = "pioneer-study-addons";

        MOCK_TIME.with(|cell| cell.set(Some(epoch_secs)));

        let some_metadata = CollectionMetadata {
            bucket: bucket.into(),
            signatures: signatures.to_vec(),
        };
        // Changeset for when client fetches diff.
        let diff_changeset = ChangesetResponse {
            changes: diff_records.to_vec(),
            timestamp: 1603992731957,
            metadata: some_metadata.clone(),
        };
        // Changeset for when client retries from scratch.
        let full_changeset = ChangesetResponse {
            changes: full_records.to_vec(),
            timestamp: 1603992731957,
            metadata: some_metadata.clone(),
        };

        let mut api_client = MockApiClient::new();
        api_client
            .expect_collection_url()
            .returning(move || format!("http://server/{}", collection_name));
        api_client.expect_is_prod_server().returning(|| Ok(false));
        api_client.expect_fetch_changeset().returning(move |since| {
            Ok(if since.is_some() {
                diff_changeset.clone()
            } else {
                full_changeset.clone()
            })
        });

        let certificate = certificate.to_string();
        api_client
            .expect_fetch_cert()
            .returning(move |_| Ok(certificate.clone().into_bytes()));

        let storage = Storage::new(":memory:".into());
        let jexl_filter = JexlFilter::new(Some(RemoteSettingsContext::default()));
        RemoteSettingsClient::new_from_parts(
            collection_name.to_string(),
            storage,
            jexl_filter,
            api_client,
        )
    }

    fn run_client_sync(
        diff_records: &[RemoteSettingsRecord],
        full_records: &[RemoteSettingsRecord],
        certificate: &str,
        signatures: &[CollectionSignature],
        epoch_secs: u64,
        bucket: &str,
    ) -> Result<()> {
        build_client(
            diff_records,
            full_records,
            certificate,
            signatures,
            epoch_secs,
            bucket,
        )
        .sync()
    }

    #[test]
    fn test_valid_signature() -> Result<()> {
        ensure_initialized();
        run_client_sync(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: VALID_SIGNATURE.to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        )
        .expect("Valid signature");
        Ok(())
    }

    #[test]
    fn test_second_signature_is_valid() -> Result<()> {
        ensure_initialized();
        run_client_sync(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[
                CollectionSignature {
                    signature: "invalid signature".to_string(),
                    x5u: "http://mocked".into(),
                    mode: "p384ecdsa".into(),
                },
                CollectionSignature {
                    signature: VALID_SIGNATURE.to_string(),
                    x5u: "http://mocked".into(),
                    mode: "p384ecdsa".into(),
                },
            ],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        )
        .expect("Valid signature");
        Ok(())
    }

    #[test]
    fn test_first_signature_has_unknown_type() -> Result<()> {
        ensure_initialized();
        run_client_sync(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[
                CollectionSignature {
                    signature: "unkown signature".to_string(),
                    x5u: "http://mocked".into(),
                    // Unknown signature type.
                    mode: "mldsa".into(),
                },
                CollectionSignature {
                    signature: VALID_SIGNATURE.to_string(),
                    x5u: "http://mocked".into(),
                    mode: "p384ecdsa".into(),
                },
            ],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        )
        .expect("Valid signature");
        Ok(())
    }

    #[test]
    fn test_valid_signature_after_retry() -> Result<()> {
        ensure_initialized();
        run_client_sync(
            &[RemoteSettingsRecord {
                id: "bad-record".to_string(),
                last_modified: 9999,
                deleted: true,
                attachment: None,
                fields: serde_json::Map::new(),
            }],
            &[],
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: VALID_SIGNATURE.to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        )
        .expect("Valid signature");
        Ok(())
    }

    #[test]
    fn test_invalid_signature_value() -> Result<()> {
        ensure_initialized();
        let err = run_client_sync(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: "invalid signature".to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        )
        .unwrap_err();
        assert!(matches!(err, Error::SignatureError(_)));
        assert_eq!(format!("{}", err), "Signature could not be verified: Signature content error: Encoded text cannot have a 6-bit remainder.");

        Ok(())
    }

    #[test]
    fn test_invalid_certificate_value() -> Result<()> {
        ensure_initialized();
        let err = run_client_sync(
            &[],
            &[],
            "some bad PEM content",
            &[CollectionSignature {
                signature: VALID_SIGNATURE.to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        )
        .unwrap_err();

        assert!(matches!(err, Error::SignatureError(_)));
        assert_eq!(
            format!("{}", err),
            "Signature could not be verified: PEM content format error: Missing PEM data"
        );

        Ok(())
    }

    #[test]
    fn test_invalid_signature_expired_cert() -> Result<()> {
        ensure_initialized();
        let december_20_2024 = 1734651582;

        let err = run_client_sync(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: VALID_SIGNATURE.to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            december_20_2024,
            "main",
        )
        .unwrap_err();

        assert!(matches!(err, Error::SignatureError(_)));
        assert_eq!(
            format!("{}", err),
            "Signature could not be verified: Certificate not yet valid or expired"
        );

        Ok(())
    }

    #[test]
    fn test_invalid_signature_invalid_data() -> Result<()> {
        ensure_initialized();
        // The signature is valid for an empty list of records.
        let records = vec![RemoteSettingsRecord {
            id: "unexpected-data".to_string(),
            last_modified: 42,
            deleted: false,
            attachment: None,
            fields: serde_json::Map::new(),
        }];
        let err = run_client_sync(
            &records,
            &records,
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: VALID_SIGNATURE.to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        )
        .unwrap_err();

        assert!(matches!(err, Error::SignatureError(_)));
        assert_eq!(format!("{}", err), "Signature could not be verified: Content signature mismatch error: NSS error: NSS error: -8182 ");

        Ok(())
    }

    #[test]
    fn test_invalid_signature_invalid_signer_name() -> Result<()> {
        ensure_initialized();
        let err = run_client_sync(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: VALID_SIGNATURE.to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "security-state",
        )
        .unwrap_err();
        assert!(matches!(err, Error::SignatureError(_)));
        assert_eq!(
            format!("{}", err),
            "Signature could not be verified: Certificate subject mismatch"
        );

        Ok(())
    }

    #[test]
    fn test_get_records_sync_if_empty_verifies_signature() -> Result<()> {
        ensure_initialized();
        let rs_client = build_client(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: "invalid signature".to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        );

        let err = rs_client.get_records(true).unwrap_err();

        assert!(matches!(err, Error::SignatureError(_)));
        assert_eq!(format!("{}", err), "Signature could not be verified: Signature content error: Encoded text cannot have a 6-bit remainder.");

        // Unverified data was not kept in storage.
        let mut inner = rs_client.lock_inner()?;
        let collection_url = inner.api_client.collection_url();
        assert_eq!(inner.storage.get_records(&collection_url)?, None);

        Ok(())
    }

    #[test]
    fn test_get_records_sync_if_empty_with_valid_signature() -> Result<()> {
        ensure_initialized();
        let rs_client = build_client(
            &[],
            &[],
            VALID_CERTIFICATE,
            &[CollectionSignature {
                signature: VALID_SIGNATURE.to_string(),
                x5u: "http://mocked".into(),
                mode: "p384ecdsa".into(),
            }],
            VALID_CERT_EPOCH_SECONDS,
            "main",
        );

        // The signature is only valid for an empty list of records.
        assert_eq!(rs_client.get_records(true)?, Some(vec![]));

        Ok(())
    }
}

#[cfg(test)]
mod test_reset_storage {
    use super::*;

    #[test]
    fn test_reset_storage_deletes_records_and_attachments() {
        let collection_url = "http://rs.example.com/v2/buckets/main/collections/test-collection";

        let mut api_client = MockApiClient::new();
        api_client
            .expect_collection_url()
            .returning(|| collection_url.into());
        api_client.expect_is_prod_server().returning(|| Ok(false));

        let records = vec![RemoteSettingsRecord {
            id: "record-0001".into(),
            last_modified: 100,
            deleted: false,
            attachment: Some(Attachment {
                filename: "test-file.bin".into(),
                mimetype: "application/octet-stream".into(),
                location: "attachments/test-file.bin".into(),
                hash: "3a6eb0790f39ac87c94f3856b2dd2c5d110e6811602261a9a923d3bb23adc8b7".into(),
                size: 4,
            }),
            fields: serde_json::Map::new(),
        }];

        let mut storage = Storage::new(":memory:".into());
        storage
            .insert_collection_content(collection_url, &records, 100, CollectionMetadata::default())
            .expect("Failed to insert records");

        storage
            .set_attachment(collection_url, "attachments/test-file.bin", b"data")
            .expect("Failed to insert attachment");

        // Verify data is present before reset
        assert!(storage.get_records(collection_url).unwrap().is_some());
        assert!(storage
            .get_attachment(collection_url, records[0].attachment.clone().unwrap())
            .unwrap()
            .is_some());

        let rs_client = RemoteSettingsClient::new_from_parts(
            "test-collection".into(),
            storage,
            JexlFilter::new(None),
            api_client,
        );

        rs_client.reset_storage().expect("Failed to reset storage");

        // After reset, both records and attachments should be gone
        let mut inner = rs_client.inner.lock();
        assert_eq!(
            inner.storage.get_records(collection_url).unwrap(),
            None,
            "Records should be deleted after reset_storage"
        );
        assert_eq!(
            inner
                .storage
                .get_attachment(collection_url, records[0].attachment.clone().unwrap(),)
                .unwrap(),
            None,
            "Attachments should be deleted after reset_storage"
        );
    }

    #[test]
    fn test_reset_storage_reverts_to_packaged_data() {
        let collection_url = "http://rs.example.com/v2/buckets/main/collections/regions";

        let mut api_client = MockApiClient::new();
        api_client
            .expect_collection_url()
            .returning(|| collection_url.into());
        // Must be prod for reset_storage to restore packaged data
        api_client.expect_is_prod_server().returning(|| Ok(true));

        let synced_records = vec![RemoteSettingsRecord {
            id: "custom-synced-record".into(),
            last_modified: 99999,
            deleted: false,
            attachment: None,
            fields: serde_json::json!({"key": "synced-value"})
                .as_object()
                .unwrap()
                .clone(),
        }];

        let mut storage = Storage::new(":memory:".into());
        storage
            .insert_collection_content(
                collection_url,
                &synced_records,
                99999,
                CollectionMetadata::default(),
            )
            .expect("Failed to insert synced records");

        // Verify synced data is present
        let records_before = storage.get_records(collection_url).unwrap().unwrap();
        assert_eq!(records_before[0].id, "custom-synced-record");

        let rs_client = RemoteSettingsClient::new_from_parts(
            "regions".into(),
            storage,
            JexlFilter::new(None),
            api_client,
        );

        rs_client.reset_storage().expect("Failed to reset storage");

        let mut inner = rs_client.inner.lock();
        let records = inner.storage.get_records(collection_url).unwrap();
        assert!(
            records.is_some(),
            "Packaged data should be restored after reset_storage on prod"
        );
        let records = records.unwrap();
        assert!(
            !records.is_empty(),
            "Packaged regions data should not be empty"
        );
        assert!(
            !records.iter().any(|r| r.id == "custom-synced-record"),
            "Synced data should be replaced by packaged data after reset"
        );
    }
}
