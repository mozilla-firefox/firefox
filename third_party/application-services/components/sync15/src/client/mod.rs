/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! A module for everything needed to be a "sync client" - ie, a device which
//! can perform a full sync of any number of collections, including managing
//! the server state.
//!
//! In general, the client is responsible for all communication with the sync server,
//! including ensuring the state is correct, and encrypting/decrypting all records
//! to and from the server. However, the actual syncing of the collections is
//! delegated to an external [crate::engine](Sync Engine).
//!
//! One exception is that the "sync client" owns one sync engine - the
//! [crate::clients_engine], which is managed internally.
mod coll_state;
mod coll_update;
mod collection_keys;
mod request;
mod state;
mod status;
mod storage_client;
mod sync;
mod sync_ids;
mod sync_multiple;
mod token;
mod util;

pub(crate) use coll_state::{CollState, LocalCollStateMachine};
pub(crate) use coll_update::{CollectionUpdate, fetch_incoming};
pub(crate) use collection_keys::CollectionKeys;
pub(crate) use request::InfoConfiguration;
pub(crate) use state::GlobalState;
pub use status::{ServiceStatus, SyncResult};
pub use storage_client::{
    SetupStorageClient, Sync15ClientResponse, Sync15StorageClient, Sync15StorageClientInit,
};
pub use sync_multiple::{
    MemoryCachedState, SyncRequestInfo, sync_multiple, sync_multiple_with_command_processor,
};
