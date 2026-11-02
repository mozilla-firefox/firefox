/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(unknown_lints, clippy::implicit_hasher)]
#![warn(rust_2018_idioms)]

pub mod bso;
#[cfg(feature = "sync-client")]
pub mod client;
// Type to describe device types
mod device_type;
// How the user's engine choices are modelled
mod engine_choices;
// Types to describe client records
mod client_types;
// Note that `clients_engine` should probably be in `sync_client`, but let's not make
// things too nested at this stage...
#[cfg(feature = "sync-client")]
pub mod clients_engine;
#[cfg(feature = "crypto")]
mod enc_payload;
#[cfg(feature = "sync-engine")]
pub mod engine;
mod error;
#[cfg(feature = "crypto")]
mod key_bundle;
#[cfg(feature = "sync-client")]
mod record_types;
mod server_timestamp;
pub mod telemetry;

pub use crate::client_types::{ClientData, RemoteClient};
pub use crate::device_type::DeviceType;
pub use crate::engine_choices::EngineChoicesModel;
pub use crate::error::{Error, Result};
#[cfg(feature = "crypto")]
pub use enc_payload::EncryptedPayload;
#[cfg(feature = "crypto")]
pub use key_bundle::KeyBundle;
pub use server_timestamp::ServerTimestamp;
pub use sync_guid::Guid;

// Collection names are almost always `static, so we use a `Cow`:
// * Either a `String` or a `'static &str` can be `.into()`'d into one of these.
// * Cloning one with a `'static &str` is extremely cheap and doesn't allocate memory.
pub type CollectionName = std::borrow::Cow<'static, str>;

// For skip_serializing_if
fn skip_if_default<T: PartialEq + Default>(v: &T) -> bool {
    *v == T::default()
}

uniffi::setup_scaffolding!("sync15");
