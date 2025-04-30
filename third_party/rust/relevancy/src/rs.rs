/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

use crate::{Error, Result};
use remote_settings::{RemoteSettingsClient, RemoteSettingsRecord};
use serde::Deserialize;
/// The Remote Settings collection name.
pub(crate) const REMOTE_SETTINGS_COLLECTION: &str = "content-relevance";

/// A trait for a client that downloads records from Remote Settings.
///
/// This trait lets tests use a mock client.
pub(crate) trait RelevancyRemoteSettingsClient {
    /// Fetches records from the Suggest Remote Settings collection.
    fn get_records(&self) -> Result<Vec<RemoteSettingsRecord>>;

    /// Fetches a record's attachment from the Suggest Remote Settings
    /// collection.
    fn get_attachment(&self, location: &RemoteSettingsRecord) -> Result<Vec<u8>>;

    /// Close any open resources
    fn close(&self);
}

impl RelevancyRemoteSettingsClient for RemoteSettingsClient {
    fn get_records(&self) -> Result<Vec<RemoteSettingsRecord>> {
        self.sync()?;
        Ok(self
            .get_records(false)
            .expect("RemoteSettingsClient::get_records() returned None after `sync()` called"))
    }

    fn get_attachment(&self, record: &RemoteSettingsRecord) -> Result<Vec<u8>> {
        Ok(self.get_attachment(record)?)
    }

    fn close(&self) {
        self.shutdown()
    }
}

impl<T: RelevancyRemoteSettingsClient> RelevancyRemoteSettingsClient for &T {
    fn get_records(&self) -> Result<Vec<RemoteSettingsRecord>> {
        (*self).get_records()
    }

    fn get_attachment(&self, record: &RemoteSettingsRecord) -> Result<Vec<u8>> {
        (*self).get_attachment(record)
    }

    fn close(&self) {
        (*self).close();
    }
}

impl<T: RelevancyRemoteSettingsClient> RelevancyRemoteSettingsClient for std::sync::Arc<T> {
    fn get_records(&self) -> Result<Vec<RemoteSettingsRecord>> {
        (**self).get_records()
    }

    fn get_attachment(&self, record: &RemoteSettingsRecord) -> Result<Vec<u8>> {
        (**self).get_attachment(record)
    }

    fn close(&self) {
        (**self).close()
    }
}

#[derive(Clone, Debug, Deserialize)]
pub struct RelevancyRecord {
    #[allow(dead_code)]
    #[serde(rename = "type")]
    pub record_type: String,
    pub record_custom_details: RecordCustomDetails,
}

// Custom details related to category of the record.
#[derive(Clone, Debug, Deserialize)]
pub struct RecordCustomDetails {
    pub category_to_domains: CategoryToDomains,
}

/// Category information related to the record.
#[derive(Clone, Debug, Deserialize)]
pub struct CategoryToDomains {
    #[allow(dead_code)]
    pub version: i32,
    #[allow(dead_code)]
    pub category: String,
    pub category_code: i32,
}

/// A downloaded Remote Settings attachment that contains domain data.
#[derive(Clone, Debug, Deserialize)]
pub struct RelevancyAttachmentData {
    pub domain: String,
}

/// Deserialize one of these types from a JSON value
pub fn from_json<T: serde::de::DeserializeOwned>(value: serde_json::Value) -> Result<T> {
    serde_path_to_error::deserialize(value).map_err(|e| Error::RemoteSettingsParseError {
        type_name: std::any::type_name::<T>().to_owned(),
        path: e.path().to_string(),
        error: e.into_inner(),
    })
}

/// Deserialize one of these types from a slice of JSON data
pub fn from_json_slice<T: serde::de::DeserializeOwned>(value: &[u8]) -> Result<T> {
    let json_value =
        serde_json::from_slice(value).map_err(|e| Error::RemoteSettingsParseError {
            type_name: std::any::type_name::<T>().to_owned(),
            path: "<while parsing JSON>".to_owned(),
            error: e,
        })?;
    from_json(json_value)
}

#[cfg(test)]
pub mod test {
    use super::*;

    // Type that implements RelevancyRemoteSettingsClient, but panics if the methods are actually
    // called.  This is used in tests that need to construct a `RelevancyStore`, but don't want to
    // construct an actual remote settings client.
    pub struct NullRelavancyRemoteSettingsClient;

    impl RelevancyRemoteSettingsClient for NullRelavancyRemoteSettingsClient {
        fn get_records(&self) -> Result<Vec<RemoteSettingsRecord>> {
            panic!("NullRelavancyRemoteSettingsClient::get_records was called")
        }

        fn get_attachment(&self, _record: &RemoteSettingsRecord) -> Result<Vec<u8>> {
            panic!("NullRelavancyRemoteSettingsClient::get_records was called")
        }

        fn close(&self) {}
    }
}
