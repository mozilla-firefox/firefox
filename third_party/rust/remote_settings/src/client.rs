/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::config::{BaseUrl, RemoteSettingsConfig};
use crate::error::{debug, trace, Error, Result};
use crate::jexl_filter::JexlFilter;
#[cfg(feature = "signatures")]
use crate::signatures;
use crate::storage::Storage;
use crate::RemoteSettingsContext;
use crate::{packaged_attachments, packaged_collections, RemoteSettingsServer};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    borrow::Cow,
    time::{Duration, Instant},
};
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
const HEADER_ETAG: &str = "ETag";
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
}

struct RemoteSettingsClientInner<C> {
    storage: Storage,
    api_client: C,
    jexl_filter: JexlFilter,
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
        }
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
        let mut inner = self.inner.lock();
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

        Ok(match (cached_records, sync_if_empty) {
            // Case 2: We have cached records
            //
            // Note: we should return these even if it's an empty list and `sync_if_empty=true`.
            // The "if empty" part refers to the cache being empty, not the list.
            (Some(cached_records), _) => Some(self.filter_records(cached_records, &inner)),
            // Case 3: sync_if_empty=true
            (None, true) => {
                let changeset = inner.api_client.fetch_changeset(None)?;
                inner.storage.insert_collection_content(
                    &collection_url,
                    &changeset.changes,
                    changeset.timestamp,
                    changeset.metadata,
                )?;
                Some(self.filter_records(changeset.changes, &inner))
            }
            // Case 4: Nothing to return
            (None, false) => None,
        })
    }

    pub fn get_last_modified_timestamp(&self) -> Result<Option<u64>> {
        let mut inner = self.inner.lock();
        let collection_url = inner.api_client.collection_url();
        inner.storage.get_last_modified_timestamp(&collection_url)
    }

    /// Synchronizes the local collection with the remote server by performing the following steps:
    /// 1. Fetches the last modified timestamp of the collection from local storage.
    /// 2. Fetches the changeset from the remote server based on the last modified timestamp.
    /// 3. Inserts the fetched changeset into local storage.
    fn perform_sync_operation(&self) -> Result<()> {
        let mut inner = self.inner.lock();
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

    fn reset_storage(&self) -> Result<()> {
        trace!("{0}: reset local storage.", self.collection_name);
        let mut inner = self.inner.lock();
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
        let mut inner = self.inner.lock();
        let collection_url = inner.api_client.collection_url();
        let timestamp = inner.storage.get_last_modified_timestamp(&collection_url)?;
        let records = inner.storage.get_records(&collection_url)?;
        let metadata = inner.storage.get_collection_metadata(&collection_url)?;
        match (timestamp, &records, metadata) {
            (Some(timestamp), Some(records), Some(metadata)) => {
                let cert_chain_bytes = inner.api_client.fetch_cert(&metadata.signature.x5u)?;
                // rc_crypto verifies that the provided certificates chain leads to our root certificate.
                let expected_root_hash = if inner.api_client.is_prod_server()? {
                    ROOT_CERT_SHA256_HASH_PROD
                } else {
                    ROOT_CERT_SHA256_HASH_NONPROD
                };

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
                signatures::verify_signature(
                    timestamp,
                    records,
                    metadata.signature.signature.as_bytes(),
                    &cert_chain_bytes,
                    epoch_seconds(),
                    expected_root_hash,
                    &expected_leaf_cname,
                )
                .inspect_err(|err| {
                    debug!(
                        "{0}: bad signature ({1:?}) using certificate {2} and signer '{3}'",
                        self.collection_name, err, &metadata.signature.x5u, expected_leaf_cname
                    );
                })?;
                trace!("{0}: signature verification success.", self.collection_name);
                Ok(())
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

        let mut inner = self.inner.lock();
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

    pub fn update_config(
        &self,
        server_url: BaseUrl,
        bucket_name: String,
        context: Option<RemoteSettingsContext>,
    ) -> Result<()> {
        let mut inner = self.inner.lock();
        inner.api_client = ViaductApiClient::new(server_url, &bucket_name, &self.collection_name);
        inner.jexl_filter = JexlFilter::new(context);
        inner.storage.empty()
    }
}

#[cfg_attr(test, mockall::automock)]
pub trait ApiClient {
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
        trace!("make_request: {url}");
        self.remote_state.ensure_no_backoff()?;

        let req = Request::get(url);
        let resp = req.send()?;

        self.remote_state.handle_backoff_hint(&resp)?;

        if resp.is_success() {
            Ok(resp)
        } else {
            Err(Error::ResponseError(format!(
                "status code: {}",
                resp.status
            )))
        }
    }
}

impl ApiClient for ViaductApiClient {
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
                .append_pair("_since", &format!("\"{}\"", timestamp));
        }

        let resp = self.make_request(url)?;

        if resp.is_success() {
            Ok(resp.json::<ChangesetResponse>()?)
        } else {
            Err(Error::ResponseError(format!(
                "status code: {}",
                resp.status
            )))
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
        let resp = self.make_request(Url::parse(x5u)?)?;
        Ok(resp.body)
    }
}

/// A simple HTTP client that can retrieve Remote Settings data using the properties by [ClientConfig].
/// Methods defined on this will fetch data from
/// <base_url>/buckets/<bucket_name>/collections/<collection_name>/
pub struct Client {
    endpoints: RemoteSettingsEndpoints,
    pub(crate) remote_state: Mutex<RemoteState>,
}

impl Client {
    /// Create a new [Client] with properties matching config.
    pub fn new(config: RemoteSettingsConfig) -> Result<Self> {
        let server = match (config.server, config.server_url) {
            (Some(server), None) => server,
            (None, Some(server_url)) => RemoteSettingsServer::Custom { url: server_url },
            (None, None) => RemoteSettingsServer::Prod,
            (Some(_), Some(_)) => Err(Error::ConfigError(
                "`RemoteSettingsConfig` takes either `server` or `server_url`, not both".into(),
            ))?,
        };

        let bucket_name = config.bucket_name.unwrap_or_else(|| String::from("main"));
        let endpoints = RemoteSettingsEndpoints::new(
            &server.get_base_url()?,
            &bucket_name,
            &config.collection_name,
        );

        Ok(Self {
            endpoints,
            remote_state: Default::default(),
        })
    }

    /// Fetches all records for a collection that can be found in the server,
    /// bucket, and collection defined by the [ClientConfig] used to generate
    /// this [Client].
    pub fn get_records(&self) -> Result<RemoteSettingsResponse> {
        self.get_records_with_options(&GetItemsOptions::new())
    }

    /// Fetches all records for a collection that can be found in the server,
    /// bucket, and collection defined by the [ClientConfig] used to generate
    /// this [Client]. This function will return the raw network [Response].
    pub fn get_records_raw(&self) -> Result<Response> {
        self.get_records_raw_with_options(&GetItemsOptions::new())
    }

    /// Fetches all records that have been published since provided timestamp
    /// for a collection that can be found in the server, bucket, and
    /// collection defined by the [ClientConfig] used to generate this [Client].
    pub fn get_records_since(&self, timestamp: u64) -> Result<RemoteSettingsResponse> {
        self.get_records_with_options(
            GetItemsOptions::new().filter_gt("last_modified", timestamp.to_string()),
        )
    }

    /// Fetches records from this client's collection with the given options.
    pub fn get_records_with_options(
        &self,
        options: &GetItemsOptions,
    ) -> Result<RemoteSettingsResponse> {
        let resp = self.get_records_raw_with_options(options)?;
        let records = resp.json::<RecordsResponse>()?.data;
        let etag = resp
            .headers
            .get(HEADER_ETAG)
            .ok_or_else(|| Error::ResponseError("no etag header".into()))?;
        // Per https://docs.kinto-storage.org/en/stable/api/1.x/timestamps.html,
        // the `ETag` header value is a quoted integer. Trim the quotes before
        // parsing.
        let last_modified = etag.trim_matches('"').parse().map_err(|_| {
            Error::ResponseError(format!(
                "expected quoted integer in etag header; got `{}`",
                etag
            ))
        })?;
        Ok(RemoteSettingsResponse {
            records,
            last_modified,
        })
    }

    /// Fetches a raw network [Response] for records from this client's
    /// collection with the given options.
    pub fn get_records_raw_with_options(&self, options: &GetItemsOptions) -> Result<Response> {
        let mut url = self.endpoints.records_url.clone();
        for (name, value) in options.iter_query_pairs() {
            url.query_pairs_mut().append_pair(&name, &value);
        }
        self.make_request(url)
    }

    /// Downloads an attachment from [attachment_location]. NOTE: there are no
    /// guarantees about a maximum size, so use care when fetching potentially
    /// large attachments.
    pub fn get_attachment(&self, attachment_location: &str) -> Result<Vec<u8>> {
        Ok(self.get_attachment_raw(attachment_location)?.body)
    }

    /// Fetches a raw network [Response] for an attachment.
    pub fn get_attachment_raw(&self, attachment_location: &str) -> Result<Response> {
        // Important: We use a `let` binding here to ensure that the mutex is
        // unlocked immediately after cloning the URL. If we matched directly on
        // the `.lock()` expression, the mutex would stay locked until the end
        // of the `match`, causing a deadlock.
        let maybe_attachments_base_url = self.remote_state.lock().attachments_base_url.clone();

        let attachments_base_url = match maybe_attachments_base_url {
            Some(attachments_base_url) => attachments_base_url,
            None => {
                let server_info = self
                    .make_request(self.endpoints.root_url.clone())?
                    .json::<ServerInfo>()?;
                let attachments_base_url = match server_info.capabilities.attachments {
                    Some(capability) => Url::parse(&capability.base_url)?,
                    None => Err(Error::AttachmentsUnsupportedError)?,
                };
                self.remote_state.lock().attachments_base_url = Some(attachments_base_url.clone());
                attachments_base_url
            }
        };

        self.make_request(attachments_base_url.join(attachment_location)?)
    }

    fn make_request(&self, url: Url) -> Result<Response> {
        let mut current_remote_state = self.remote_state.lock();
        current_remote_state.ensure_no_backoff()?;
        drop(current_remote_state);

        let req = Request::get(url);
        let resp = req.send()?;

        let mut current_remote_state = self.remote_state.lock();
        current_remote_state.handle_backoff_hint(&resp)?;

        if resp.is_success() {
            Ok(resp)
        } else {
            Err(Error::ResponseError(format!(
                "status code: {}",
                resp.status
            )))
        }
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
    /// URL for the records request
    ///
    /// This has the form:
    /// `[base-url]/buckets/[bucket-name]/collections/[collection-name]/records`.
    ///
    /// This is the old/deprecated way to get records
    records_url: Url,
}

impl RemoteSettingsEndpoints {
    /// Construct a new RemoteSettingsEndpoints
    ///
    /// `base_url` should have the form `https://[domain]/v1` (no trailing slash).
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

        let mut records_url = collection_url.clone();
        records_url.path_segments_mut().push("records");

        let mut changeset_url = collection_url.clone();
        changeset_url.path_segments_mut().push("changeset");

        Self {
            root_url: root_url.into_inner(),
            collection_url: collection_url.into_inner(),
            records_url: records_url.into_inner(),
            changeset_url: changeset_url.into_inner(),
        }
    }
}

/// Data structure representing the top-level response from the Remote Settings.
/// [last_modified] will be extracted from the etag header of the response.
#[derive(Clone, Debug, Eq, PartialEq, Deserialize, Serialize, uniffi::Record)]
pub struct RemoteSettingsResponse {
    pub records: Vec<RemoteSettingsRecord>,
    pub last_modified: u64,
}

#[derive(Deserialize, Serialize)]
struct RecordsResponse {
    data: Vec<RemoteSettingsRecord>,
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
    pub signature: CollectionSignature,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize, Eq, PartialEq)]
pub struct CollectionSignature {
    pub signature: String,
    /// X.509 certificate chain Url (x5u)
    pub x5u: String,
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

/// Options for requests to endpoints that return multiple items.
#[derive(Clone, Debug, Default, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct GetItemsOptions {
    filters: Vec<Filter>,
    sort: Vec<Sort>,
    fields: Vec<String>,
    limit: Option<u64>,
}

impl GetItemsOptions {
    /// Creates an empty option set.
    pub fn new() -> Self {
        Self::default()
    }

    /// Sets an option to only return items whose `field` is equal to the given
    /// `value`.
    ///
    /// `field` can be a simple or dotted field name, like `author` or
    /// `author.name`. `value` can be a bare number or string (like
    /// `2` or `Ben`), or a stringified JSON value (`"2.0"`, `[1, 2]`,
    /// `{"checked": true}`).
    pub fn filter_eq(&mut self, field: impl Into<String>, value: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Eq(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items whose `field` is not equal to the
    /// given `value`.
    pub fn filter_not(&mut self, field: impl Into<String>, value: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Not(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items whose `field` is an array that
    /// contains the given `value`. If `value` is a stringified JSON array, the
    /// field must contain all its elements.
    pub fn filter_contains(
        &mut self,
        field: impl Into<String>,
        value: impl Into<String>,
    ) -> &mut Self {
        self.filters
            .push(Filter::Contains(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items whose `field` is strictly less
    /// than the given `value`.
    pub fn filter_lt(&mut self, field: impl Into<String>, value: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Lt(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items whose `field` is strictly greater
    /// than the given `value`.
    pub fn filter_gt(&mut self, field: impl Into<String>, value: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Gt(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items whose `field` is less than or equal
    /// to the given `value`.
    pub fn filter_max(&mut self, field: impl Into<String>, value: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Max(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items whose `field` is greater than or
    /// equal to the given `value`.
    pub fn filter_min(&mut self, field: impl Into<String>, value: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Min(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items whose `field` is a string that
    /// contains the substring `value`. `value` can contain `*` wildcards.
    pub fn filter_like(&mut self, field: impl Into<String>, value: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Like(field.into(), value.into()));
        self
    }

    /// Sets an option to only return items that have the given `field`.
    pub fn filter_has(&mut self, field: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::Has(field.into()));
        self
    }

    /// Sets an option to only return items that do not have the given `field`.
    pub fn filter_has_not(&mut self, field: impl Into<String>) -> &mut Self {
        self.filters.push(Filter::HasNot(field.into()));
        self
    }

    /// Sets an option to return items in `order` for the given `field`.
    pub fn sort(&mut self, field: impl Into<String>, order: SortOrder) -> &mut Self {
        self.sort.push(Sort(field.into(), order));
        self
    }

    /// Sets an option to only return the given `field` of each item.
    ///
    /// The special `id` and `last_modified` fields are always returned.
    pub fn field(&mut self, field: impl Into<String>) -> &mut Self {
        self.fields.push(field.into());
        self
    }

    /// Sets the option to return at most `count` items.
    pub fn limit(&mut self, count: u64) -> &mut Self {
        self.limit = Some(count);
        self
    }

    /// Returns an iterator of (name, value) query pairs for these options.
    pub fn iter_query_pairs(&self) -> impl Iterator<Item = (Cow<'_, str>, Cow<'_, str>)> {
        self.filters
            .iter()
            .map(Filter::as_query_pair)
            .chain({
                // For sorting (https://docs.kinto-storage.org/en/latest/api/1.x/sorting.html),
                // the query pair syntax is `_sort=field1,-field2`, where the
                // fields to sort by are specified in a comma-separated ordered
                // list, and `-` indicates descending order.
                (!self.sort.is_empty()).then(|| {
                    (
                        "_sort".into(),
                        (self
                            .sort
                            .iter()
                            .map(Sort::as_query_value)
                            .collect::<Vec<_>>()
                            .join(","))
                        .into(),
                    )
                })
            })
            .chain({
                // For selecting fields (https://docs.kinto-storage.org/en/latest/api/1.x/selecting_fields.html),
                // the query pair syntax is `_fields=field1,field2`.
                (!self.fields.is_empty()).then(|| ("_fields".into(), self.fields.join(",").into()))
            })
            .chain({
                // For pagination (https://docs.kinto-storage.org/en/latest/api/1.x/pagination.html),
                // the query pair syntax is `_limit={count}`.
                self.limit
                    .map(|count| ("_limit".into(), count.to_string().into()))
            })
    }
}

/// The order in which to return items.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, PartialOrd, Ord)]
pub enum SortOrder {
    /// Smaller values first.
    Ascending,
    /// Larger values first.
    Descending,
}

#[derive(Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
enum Filter {
    Eq(String, String),
    Not(String, String),
    Contains(String, String),
    Lt(String, String),
    Gt(String, String),
    Max(String, String),
    Min(String, String),
    Like(String, String),
    Has(String),
    HasNot(String),
}

impl Filter {
    fn as_query_pair(&self) -> (Cow<'_, str>, Cow<'_, str>) {
        // For filters (https://docs.kinto-storage.org/en/latest/api/1.x/filtering.html),
        // the query pair syntax is `[operator_]field=value` for each field.
        match self {
            Filter::Eq(field, value) => (field.into(), value.into()),
            Filter::Not(field, value) => (format!("not_{field}").into(), value.into()),
            Filter::Contains(field, value) => (format!("contains_{field}").into(), value.into()),
            Filter::Lt(field, value) => (format!("lt_{field}").into(), value.into()),
            Filter::Gt(field, value) => (format!("gt_{field}").into(), value.into()),
            Filter::Max(field, value) => (format!("max_{field}").into(), value.into()),
            Filter::Min(field, value) => (format!("min_{field}").into(), value.into()),
            Filter::Like(field, value) => (format!("like_{field}").into(), value.into()),
            Filter::Has(field) => (format!("has_{field}").into(), "true".into()),
            Filter::HasNot(field) => (format!("has_{field}").into(), "false".into()),
        }
    }
}

#[derive(Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
struct Sort(String, SortOrder);

impl Sort {
    fn as_query_value(&self) -> Cow<'_, str> {
        match self.1 {
            SortOrder::Ascending => self.0.as_str().into(),
            SortOrder::Descending => format!("-{}", self.0).into(),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use expect_test::expect;
    use mockito::{mock, Matcher};
    #[test]
    fn test_defaults() {
        let config = RemoteSettingsConfig {
            server: None,
            server_url: None,
            bucket_name: None,
            collection_name: String::from("the-collection"),
        };
        let client = Client::new(config).unwrap();
        assert_eq!(
            Url::parse("https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/the-collection").unwrap(),
            client.endpoints.collection_url
        );
    }

    #[test]
    fn test_deprecated_server_url() {
        let config = RemoteSettingsConfig {
            server: None,
            server_url: Some("https://example.com".into()),
            bucket_name: None,
            collection_name: String::from("the-collection"),
        };
        let client = Client::new(config).unwrap();
        assert_eq!(
            Url::parse("https://example.com/v1/buckets/main/collections/the-collection").unwrap(),
            client.endpoints.collection_url
        );
    }

    #[test]
    fn test_invalid_config() {
        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Prod),
            server_url: Some("https://example.com".into()),
            bucket_name: None,
            collection_name: String::from("the-collection"),
        };
        match Client::new(config) {
            Ok(_) => panic!("Wanted config error; got client"),
            Err(Error::ConfigError(_)) => {}
            Err(err) => panic!("Wanted config error; got {}", err),
        }
    }

    #[test]
    fn test_attachment_can_be_downloaded() {
        viaduct_dev::init_backend_dev();
        let server_info_m = mock("GET", "/v1/")
            .with_body(attachment_metadata(mockito::server_url()))
            .with_status(200)
            .with_header("content-type", "application/json")
            .create();

        let attachment_location = "123.jpg";
        let attachment_bytes: Vec<u8> = "I'm a JPG, I swear".into();
        let attachment_m = mock(
            "GET",
            format!("/attachments/{}", attachment_location).as_str(),
        )
        .with_body(attachment_bytes.clone())
        .with_status(200)
        .with_header("content-type", "application/json")
        .create();

        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            collection_name: String::from("the-collection"),
            bucket_name: None,
        };

        let client = Client::new(config).unwrap();
        let first_resp = client.get_attachment(attachment_location).unwrap();
        let second_resp = client.get_attachment(attachment_location).unwrap();

        server_info_m.expect(1).assert();
        attachment_m.expect(2).assert();
        assert_eq!(first_resp, attachment_bytes);
        assert_eq!(second_resp, attachment_bytes);
    }

    #[test]
    fn test_attachment_errors_if_server_not_configured_for_attachments() {
        viaduct_dev::init_backend_dev();
        let server_info_m = mock("GET", "/v1/")
            .with_body(NO_ATTACHMENTS_METADATA)
            .with_status(200)
            .with_header("content-type", "application/json")
            .create();

        let attachment_location = "123.jpg";
        let attachment_bytes: Vec<u8> = "I'm a JPG, I swear".into();
        let attachment_m = mock(
            "GET",
            format!("/attachments/{}", attachment_location).as_str(),
        )
        .with_body(attachment_bytes)
        .with_status(200)
        .with_header("content-type", "application/json")
        .create();

        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            collection_name: String::from("the-collection"),
            bucket_name: None,
        };

        let client = Client::new(config).unwrap();
        let resp = client.get_attachment(attachment_location);
        server_info_m.expect(1).assert();
        attachment_m.expect(0).assert();
        assert!(matches!(resp, Err(Error::AttachmentsUnsupportedError)))
    }

    #[test]
    fn test_backoff() {
        viaduct_dev::init_backend_dev();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("Backoff", "60")
        .with_header("etag", "\"1000\"")
        .create();
        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            collection_name: String::from("the-collection"),
            bucket_name: Some(String::from("the-bucket")),
        };
        let http_client = Client::new(config).unwrap();

        assert!(http_client.get_records().is_ok());
        let second_resp = http_client.get_records();
        assert!(matches!(second_resp, Err(Error::BackoffError(_))));
        m.expect(1).assert();
    }

    #[test]
    fn test_500_retry_after() {
        viaduct_dev::init_backend_dev();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .with_body("Boom!")
        .with_status(500)
        .with_header("Retry-After", "60")
        .create();
        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            collection_name: String::from("the-collection"),
            bucket_name: Some(String::from("the-bucket")),
        };
        let http_client = Client::new(config).unwrap();
        assert!(http_client.get_records().is_err());
        let second_request = http_client.get_records();
        assert!(matches!(second_request, Err(Error::BackoffError(_))));
        m.expect(1).assert();
    }

    #[test]
    fn test_options() {
        viaduct_dev::init_backend_dev();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .match_query(Matcher::AllOf(vec![
            Matcher::UrlEncoded("a".into(), "b".into()),
            Matcher::UrlEncoded("lt_c.d".into(), "5".into()),
            Matcher::UrlEncoded("gt_e".into(), "15".into()),
            Matcher::UrlEncoded("max_f".into(), "20".into()),
            Matcher::UrlEncoded("min_g".into(), "10".into()),
            Matcher::UrlEncoded("not_h".into(), "i".into()),
            Matcher::UrlEncoded("like_j".into(), "*k*".into()),
            Matcher::UrlEncoded("has_l".into(), "true".into()),
            Matcher::UrlEncoded("has_m".into(), "false".into()),
            Matcher::UrlEncoded("contains_n".into(), "o".into()),
            Matcher::UrlEncoded("_sort".into(), "-b,a".into()),
            Matcher::UrlEncoded("_fields".into(), "a,c,b".into()),
            Matcher::UrlEncoded("_limit".into(), "3".into()),
        ]))
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();
        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            collection_name: String::from("the-collection"),
            bucket_name: Some(String::from("the-bucket")),
        };
        let http_client = Client::new(config).unwrap();
        let mut options = GetItemsOptions::new();
        options
            .field("a")
            .field("c")
            .field("b")
            .filter_eq("a", "b")
            .filter_lt("c.d", "5")
            .filter_gt("e", "15")
            .filter_max("f", "20")
            .filter_min("g", "10")
            .filter_not("h", "i")
            .filter_like("j", "*k*")
            .filter_has("l")
            .filter_has_not("m")
            .filter_contains("n", "o")
            .sort("b", SortOrder::Descending)
            .sort("a", SortOrder::Ascending)
            .limit(3);

        assert!(http_client.get_records_raw_with_options(&options).is_ok());
        expect![[r#"
            RemoteSettingsResponse {
                records: [
                    RemoteSettingsRecord {
                        id: "c5dcd1da-7126-4abb-846b-ec85b0d4d0d7",
                        last_modified: 1677694949407,
                        deleted: false,
                        attachment: Some(
                            Attachment {
                                filename: "jgp-attachment.jpg",
                                mimetype: "image/jpeg",
                                location: "the-bucket/the-collection/d3a5eccc-f0ca-42c3-b0bb-c0d4408c21c9.jpg",
                                hash: "2cbd593f3fd5f1585f92265433a6696a863bc98726f03e7222135ff0d8e83543",
                                size: 1374325,
                            },
                        ),
                        fields: {
                            "title": String("jpg-attachment"),
                            "content": String("content"),
                            "schema": Number(1677694447771),
                        },
                    },
                    RemoteSettingsRecord {
                        id: "ff301910-6bf5-4cfe-bc4c-5c80308661a5",
                        last_modified: 1677694470354,
                        deleted: false,
                        attachment: Some(
                            Attachment {
                                filename: "pdf-attachment.pdf",
                                mimetype: "application/pdf",
                                location: "the-bucket/the-collection/5f7347c2-af92-411d-a65b-f794f9b5084c.pdf",
                                hash: "de1cde3571ef3faa77ea0493276de9231acaa6f6651602e93aa1036f51181e9b",
                                size: 157,
                            },
                        ),
                        fields: {
                            "title": String("with-attachment"),
                            "content": String("content"),
                            "schema": Number(1677694447771),
                        },
                    },
                    RemoteSettingsRecord {
                        id: "7403c6f9-79be-4e0c-a37a-8f2b5bd7ad58",
                        last_modified: 1677694455368,
                        deleted: false,
                        attachment: None,
                        fields: {
                            "title": String("no-attachment"),
                            "content": String("content"),
                            "schema": Number(1677694447771),
                        },
                    },
                    RemoteSettingsRecord {
                        id: "9320f53c-0a39-4997-9120-62ff597ffb26",
                        last_modified: 1690921847416,
                        deleted: true,
                        attachment: None,
                        fields: {},
                    },
                ],
                last_modified: 1000,
            }
        "#]].assert_debug_eq(&http_client
            .get_records_with_options(&options)
            .unwrap());
        m.expect(2).assert();
    }

    #[test]
    fn test_backoff_recovery() {
        viaduct_dev::init_backend_dev();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();
        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            collection_name: String::from("the-collection"),
            bucket_name: Some(String::from("the-bucket")),
        };
        let http_client = Client::new(config).unwrap();
        // First, sanity check that manipulating the remote state does something.
        let mut current_remote_state = http_client.remote_state.lock();
        current_remote_state.backoff = BackoffState::Backoff {
            observed_at: Instant::now(),
            duration: Duration::from_secs(30),
        };
        drop(current_remote_state);
        assert!(matches!(
            http_client.get_records(),
            Err(Error::BackoffError(_))
        ));
        // Then do the actual test.
        let mut current_remote_state = http_client.remote_state.lock();
        current_remote_state.backoff = BackoffState::Backoff {
            observed_at: Instant::now() - Duration::from_secs(31),
            duration: Duration::from_secs(30),
        };
        drop(current_remote_state);
        assert!(http_client.get_records().is_ok());
        m.expect(1).assert();
    }

    #[test]
    fn test_record_fields() {
        viaduct_dev::init_backend_dev();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();
        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            collection_name: String::from("the-collection"),
            bucket_name: Some(String::from("the-bucket")),
        };
        let http_client = Client::new(config).unwrap();
        let response = http_client.get_records().unwrap();
        expect![[r#"
            RemoteSettingsResponse {
                records: [
                    RemoteSettingsRecord {
                        id: "c5dcd1da-7126-4abb-846b-ec85b0d4d0d7",
                        last_modified: 1677694949407,
                        deleted: false,
                        attachment: Some(
                            Attachment {
                                filename: "jgp-attachment.jpg",
                                mimetype: "image/jpeg",
                                location: "the-bucket/the-collection/d3a5eccc-f0ca-42c3-b0bb-c0d4408c21c9.jpg",
                                hash: "2cbd593f3fd5f1585f92265433a6696a863bc98726f03e7222135ff0d8e83543",
                                size: 1374325,
                            },
                        ),
                        fields: {
                            "title": String("jpg-attachment"),
                            "content": String("content"),
                            "schema": Number(1677694447771),
                        },
                    },
                    RemoteSettingsRecord {
                        id: "ff301910-6bf5-4cfe-bc4c-5c80308661a5",
                        last_modified: 1677694470354,
                        deleted: false,
                        attachment: Some(
                            Attachment {
                                filename: "pdf-attachment.pdf",
                                mimetype: "application/pdf",
                                location: "the-bucket/the-collection/5f7347c2-af92-411d-a65b-f794f9b5084c.pdf",
                                hash: "de1cde3571ef3faa77ea0493276de9231acaa6f6651602e93aa1036f51181e9b",
                                size: 157,
                            },
                        ),
                        fields: {
                            "title": String("with-attachment"),
                            "content": String("content"),
                            "schema": Number(1677694447771),
                        },
                    },
                    RemoteSettingsRecord {
                        id: "7403c6f9-79be-4e0c-a37a-8f2b5bd7ad58",
                        last_modified: 1677694455368,
                        deleted: false,
                        attachment: None,
                        fields: {
                            "title": String("no-attachment"),
                            "content": String("content"),
                            "schema": Number(1677694447771),
                        },
                    },
                    RemoteSettingsRecord {
                        id: "9320f53c-0a39-4997-9120-62ff597ffb26",
                        last_modified: 1690921847416,
                        deleted: true,
                        attachment: None,
                        fields: {},
                    },
                ],
                last_modified: 1000,
            }
        "#]].assert_debug_eq(&response);
        m.expect(1).assert();
    }

    #[test]
    fn test_missing_etag() {
        viaduct_dev::init_backend_dev();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .create();

        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            bucket_name: Some(String::from("the-bucket")),
            collection_name: String::from("the-collection"),
        };
        let client = Client::new(config).unwrap();

        let err = client.get_records().unwrap_err();
        assert!(
            matches!(err, Error::ResponseError(_)),
            "Want response error for missing `ETag`; got {}",
            err
        );
        m.expect(1).assert();
    }

    #[test]
    fn test_invalid_etag() {
        viaduct_dev::init_backend_dev();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "bad!")
        .create();

        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            server_url: None,
            bucket_name: Some(String::from("the-bucket")),
            collection_name: String::from("the-collection"),
        };
        let client = Client::new(config).unwrap();

        let err = client.get_records().unwrap_err();
        assert!(
            matches!(err, Error::ResponseError(_)),
            "Want response error for invalid `ETag`; got {}",
            err
        );
        m.expect(1).assert();
    }

    fn attachment_metadata(base_url: String) -> String {
        format!(
            r#"
            {{
                "capabilities": {{
                    "admin": {{
                        "description": "Serves the admin console.",
                        "url": "https://github.com/Kinto/kinto-admin/",
                        "version": "2.0.0"
                    }},
                    "attachments": {{
                        "description": "Add file attachments to records",
                        "url": "https://github.com/Kinto/kinto-attachment/",
                        "version": "6.3.1",
                        "base_url": "{}/attachments/"
                    }}
                }}
            }}
    "#,
            base_url
        )
    }

    const NO_ATTACHMENTS_METADATA: &str = r#"
    {
      "capabilities": {
          "admin": {
            "description": "Serves the admin console.",
            "url": "https://github.com/Kinto/kinto-admin/",
            "version": "2.0.0"
          }
      }
    }
  "#;

    fn response_body() -> String {
        format!(
            r#"
        {{
            "data": [
                {},
                {},
                {},
                {}
            ]
          }}"#,
            JPG_ATTACHMENT, PDF_ATTACHMENT, NO_ATTACHMENT, TOMBSTONE
        )
    }

    const JPG_ATTACHMENT: &str = r#"
    {
      "title": "jpg-attachment",
      "content": "content",
      "attachment": {
          "filename": "jgp-attachment.jpg",
          "location": "the-bucket/the-collection/d3a5eccc-f0ca-42c3-b0bb-c0d4408c21c9.jpg",
          "hash": "2cbd593f3fd5f1585f92265433a6696a863bc98726f03e7222135ff0d8e83543",
          "mimetype": "image/jpeg",
          "size": 1374325
      },
      "id": "c5dcd1da-7126-4abb-846b-ec85b0d4d0d7",
      "schema": 1677694447771,
      "last_modified": 1677694949407
    }
  "#;

    const PDF_ATTACHMENT: &str = r#"
    {
      "title": "with-attachment",
      "content": "content",
      "attachment": {
          "filename": "pdf-attachment.pdf",
          "location": "the-bucket/the-collection/5f7347c2-af92-411d-a65b-f794f9b5084c.pdf",
          "hash": "de1cde3571ef3faa77ea0493276de9231acaa6f6651602e93aa1036f51181e9b",
          "mimetype": "application/pdf",
          "size": 157
      },
      "id": "ff301910-6bf5-4cfe-bc4c-5c80308661a5",
      "schema": 1677694447771,
      "last_modified": 1677694470354
    }
  "#;

    const NO_ATTACHMENT: &str = r#"
      {
        "title": "no-attachment",
        "content": "content",
        "schema": 1677694447771,
        "id": "7403c6f9-79be-4e0c-a37a-8f2b5bd7ad58",
        "last_modified": 1677694455368
      }
    "#;

    const TOMBSTONE: &str = r#"
    {
      "id": "9320f53c-0a39-4997-9120-62ff597ffb26",
      "last_modified": 1690921847416,
      "deleted": true
    }
  "#;
}

#[cfg(test)]
mod test_new_client {
    use super::*;

    #[test]
    fn test_endpoints() {
        let endpoints = RemoteSettingsEndpoints::new(
            &BaseUrl::parse("http://rs.example.com/v1").unwrap(),
            "main",
            "test-collection",
        );
        assert_eq!(endpoints.root_url.to_string(), "http://rs.example.com/v1/");
        assert_eq!(
            endpoints.collection_url.to_string(),
            "http://rs.example.com/v1/buckets/main/collections/test-collection",
        );
        assert_eq!(
            endpoints.records_url.to_string(),
            "http://rs.example.com/v1/buckets/main/collections/test-collection/records",
        );
        assert_eq!(
            endpoints.changeset_url.to_string(),
            "http://rs.example.com/v1/buckets/main/collections/test-collection/changeset",
        );
    }
}

#[cfg(test)]
mod jexl_tests {
    use super::*;

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
            "http://rs.example.com/v1/buckets/main/collections/test-collection".into()
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
            "http://rs.example.com/v1/buckets/main/collections/test-collection",
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
            "http://rs.example.com/v1/buckets/main/collections/test-collection".into()
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
            "http://rs.example.com/v1/buckets/main/collections/test-collection",
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
            "http://rs.example.com/v1/buckets/main/collections/test-collection".into()
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
            "http://rs.example.com/v1/buckets/main/collections/test-collection",
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
}

#[cfg(feature = "signatures")]
#[cfg(test)]
mod test_signatures {
    use core::assert_eq;

    use crate::RemoteSettingsContext;

    use super::*;
    use nss::ensure_initialized;

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

    fn run_client_sync(
        diff_records: &[RemoteSettingsRecord],
        full_records: &[RemoteSettingsRecord],
        certificate: &str,
        signature: &str,
        epoch_secs: u64,
        bucket: &str,
    ) -> Result<()> {
        let collection_name = "pioneer-study-addons";

        MOCK_TIME.with(|cell| cell.set(Some(epoch_secs)));

        let some_metadata = CollectionMetadata {
            bucket: bucket.into(),
            signature: CollectionSignature {
                signature: signature.to_string(),
                x5u: "http://mocked".into(),
            },
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
        let rs_client = RemoteSettingsClient::new_from_parts(
            collection_name.to_string(),
            storage,
            jexl_filter,
            api_client,
        );

        rs_client.sync()
    }

    #[test]
    fn test_valid_signature() -> Result<()> {
        ensure_initialized();
        run_client_sync(
            &[],
            &[],
            VALID_CERTIFICATE,
            VALID_SIGNATURE,
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
            VALID_SIGNATURE,
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
            "invalid signature",
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
            VALID_SIGNATURE,
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
            VALID_SIGNATURE,
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
            VALID_SIGNATURE,
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
            VALID_SIGNATURE,
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
}
