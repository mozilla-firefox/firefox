/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(unknown_lints)]
#![warn(rust_2018_idioms)]

mod api;
mod db;
pub mod error;
mod ffi;
mod migration;
mod schema;
pub mod store;
mod sync;

pub use migration::MigrationInfo;

// We publish some constants from non-public modules.
pub use sync::STORAGE_VERSION;

pub use api::SYNC_MAX_ITEMS;
pub use api::SYNC_QUOTA_BYTES;
pub use api::SYNC_QUOTA_BYTES_PER_ITEM;

pub use crate::error::{QuotaReason, WebExtStorageApiError};
pub use crate::store::WebExtStorageStore;
pub use crate::sync::{bridge::WebExtStorageBridgedEngine, SyncedExtensionChange};
pub use api::UsageInfo;
pub use api::{StorageChanges, StorageValueChange};

uniffi::include_scaffolding!("webext-storage");

use serde_json::Value as JsonValue;

uniffi::custom_type!(JsonValue, String, {
    remote,
    try_lift: |val| Ok(serde_json::from_str(val.as_str())?),
    lower: |obj| obj.to_string(),
});
