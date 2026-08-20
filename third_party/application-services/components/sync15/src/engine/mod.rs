/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! This module is used by crates which need to implement a "sync engine".
//! At a high-level, a "sync engine" is code which knows how to take records
//! from a sync server, apply and reconcile them with the local data, then
//! provide records which should be uploaded to the server.
//!
//! Note that the "sync engine" does not itself talk to the server, nor does
//! it manage the state of the remote server, nor does it do any of the
//! encryption/decryption - that is the responsbility of the "sync client", as
//! implemented in the [client] module (or in some cases, implemented externally)
//!
//! [SyncEngine](crate::engine::sync_engine::SyncEngine) is a trait which works
//! on desktop and mobile. Engines implement it once and are driven two ways:
//! * On mobile, via the [sync manager](crate::sync_manager). Engines manage
//!   their own last-sync time internally.
//! * On Desktop, by the JS Sync framework via `BridgedEngineWrapper` and the
//!   `uniffi_bridged_engine!` macro.
mod bridged_engine;
mod request;
mod sync_engine;

pub use bridged_engine::BridgedEngineWrapper;
#[cfg(feature = "sync-client")]
pub(crate) use request::CollectionPost;

pub use request::{CollectionRequest, RequestOrder};
pub use sync_engine::{CollSyncIds, EngineSyncAssociation, SyncEngine, SyncEngineId};
