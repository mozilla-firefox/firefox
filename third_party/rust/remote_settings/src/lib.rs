/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{collections::HashMap, fs::File, io::prelude::Write, sync::Arc};

use error_support::{convert_log_report_error, handle_error};

pub mod cache;
pub mod client;
pub mod config;
pub mod context;
pub mod error;
pub mod schema;
pub mod service;
#[cfg(feature = "signatures")]
pub(crate) mod signatures;
pub mod storage;

pub(crate) mod jexl_filter;
mod macros;

pub use client::{Attachment, RemoteSettingsRecord, RemoteSettingsResponse, RsJsonObject};
pub use config::{BaseUrl, RemoteSettingsConfig, RemoteSettingsConfig2, RemoteSettingsServer};
pub use context::RemoteSettingsContext;
pub use error::{ApiResult, RemoteSettingsError, Result};

use client::Client;
use error::Error;
use storage::Storage;

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
    pub fn new(storage_dir: String, config: RemoteSettingsConfig2) -> Self {
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
    pub fn update_config(&self, config: RemoteSettingsConfig2) -> ApiResult<()> {
        self.internal.update_config(config)
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
                log::trace!("get_records error: {e}");
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

#[derive(uniffi::Object)]
pub struct RemoteSettings {
    pub config: RemoteSettingsConfig,
    client: Client,
}

#[uniffi::export]
impl RemoteSettings {
    /// Construct a new Remote Settings client with the given configuration.
    #[uniffi::constructor]
    #[handle_error(Error)]
    pub fn new(remote_settings_config: RemoteSettingsConfig) -> ApiResult<Self> {
        Ok(RemoteSettings {
            config: remote_settings_config.clone(),
            client: Client::new(remote_settings_config)?,
        })
    }

    /// Fetch all records for the configuration this client was initialized with.
    #[handle_error(Error)]
    pub fn get_records(&self) -> ApiResult<RemoteSettingsResponse> {
        let resp = self.client.get_records()?;
        Ok(resp)
    }

    /// Fetch all records added to the server since the provided timestamp,
    /// using the configuration this client was initialized with.
    #[handle_error(Error)]
    pub fn get_records_since(&self, timestamp: u64) -> ApiResult<RemoteSettingsResponse> {
        let resp = self.client.get_records_since(timestamp)?;
        Ok(resp)
    }

    /// Download an attachment with the provided id to the provided path.
    #[handle_error(Error)]
    pub fn download_attachment_to_path(
        &self,
        attachment_id: String,
        path: String,
    ) -> ApiResult<()> {
        let resp = self.client.get_attachment(&attachment_id)?;
        let mut file = File::create(path)?;
        file.write_all(&resp)?;
        Ok(())
    }
}

// Public functions that we don't expose via UniFFI.
//
// The long-term plan is to create a new remote settings client, transition nimbus + suggest to the
// new API, then delete this code.
impl RemoteSettings {
    /// Fetches all records for a collection that can be found in the server,
    /// bucket, and collection defined by the [ClientConfig] used to generate
    /// this [Client]. This function will return the raw viaduct [Response].
    #[handle_error(Error)]
    pub fn get_records_raw(&self) -> ApiResult<viaduct::Response> {
        self.client.get_records_raw()
    }

    /// Downloads an attachment from [attachment_location]. NOTE: there are no
    /// guarantees about a maximum size, so use care when fetching potentially
    /// large attachments.
    #[handle_error(Error)]
    pub fn get_attachment(&self, attachment_location: &str) -> ApiResult<Vec<u8>> {
        self.client.get_attachment(attachment_location)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::RemoteSettingsRecord;
    use mockito::{mock, Matcher};

    #[test]
    fn test_get_records() {
        viaduct_reqwest::use_reqwest_backend();
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
            bucket_name: Some(String::from("the-bucket")),
            collection_name: String::from("the-collection"),
        };
        let remote_settings = RemoteSettings::new(config).unwrap();

        let resp = remote_settings.get_records().unwrap();

        assert!(are_equal_json(JPG_ATTACHMENT, &resp.records[0]));
        assert_eq!(1000, resp.last_modified);
        m.expect(1).assert();
    }

    #[test]
    fn test_get_records_since() {
        viaduct_reqwest::use_reqwest_backend();
        let m = mock(
            "GET",
            "/v1/buckets/the-bucket/collections/the-collection/records",
        )
        .match_query(Matcher::UrlEncoded("gt_last_modified".into(), "500".into()))
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
            bucket_name: Some(String::from("the-bucket")),
            collection_name: String::from("the-collection"),
        };
        let remote_settings = RemoteSettings::new(config).unwrap();

        let resp = remote_settings.get_records_since(500).unwrap();
        assert!(are_equal_json(JPG_ATTACHMENT, &resp.records[0]));
        assert_eq!(1000, resp.last_modified);
        m.expect(1).assert();
    }

    // This test was designed as a proof-of-concept and requires a locally-run Remote Settings server.
    // If this were to be included in CI, it would require pulling the RS docker image and scripting
    // its configuration, as well as dynamically finding the attachment id, which would more closely
    // mimic a real world usecase.
    // #[test]
    #[allow(dead_code)]
    fn test_download() {
        viaduct_reqwest::use_reqwest_backend();
        let config = RemoteSettingsConfig {
            server: Some(RemoteSettingsServer::Custom {
                url: "http://localhost:8888".into(),
            }),
            server_url: None,
            bucket_name: Some(String::from("the-bucket")),
            collection_name: String::from("the-collection"),
        };
        let remote_settings = RemoteSettings::new(config).unwrap();

        remote_settings
            .download_attachment_to_path(
                "d3a5eccc-f0ca-42c3-b0bb-c0d4408c21c9.jpg".to_string(),
                "test.jpg".to_string(),
            )
            .unwrap();
    }

    fn are_equal_json(str: &str, rec: &RemoteSettingsRecord) -> bool {
        let r1: RemoteSettingsRecord = serde_json::from_str(str).unwrap();
        &r1 == rec
    }

    fn response_body() -> String {
        format!(
            r#"
        {{
            "data": [
                {},
                {},
                {}
            ]
          }}"#,
            JPG_ATTACHMENT, PDF_ATTACHMENT, NO_ATTACHMENT
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
}
