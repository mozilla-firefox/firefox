/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! The client engine is a [crate::engine](Sync Engine) used to manage the
//! "clients" collection. The clients engine manages the client record for
//! "this device, and also manages "commands".
//! In short, commands target one or more engines and instruct them to
//! perform various operations - such as wiping all local data.
//! These commands are used very rarely - currently the only command used
//! in practice is for bookmarks to wipe all their data, which is sent when
//! a desktop device restores all bookmarks from a backup. In this scenario,
//! desktop will delete all local bookmarks then replace them with the backed
//! up set, which without a "wipe" command would almost certainly cause other
//! connected devices to "resurrect" the deleted bookmarks.
use std::collections::HashSet;

mod engine;
mod record;
mod ser;

use crate::DeviceType;
use anyhow::Result;
pub use engine::Engine;

// These are what desktop uses.
const CLIENTS_TTL: u32 = 15_552_000; // 180 days
pub(crate) const CLIENTS_TTL_REFRESH: u64 = 604_800; // 7 days

/// A command processor applies incoming commands like wipes and resets for all
/// stores, and returns commands to send to other clients. It also manages
/// settings like the device name and type, which is stored in the special
/// `clients` collection.
///
/// In practice, this trait only has one implementation, in the sync manager.
/// It's split this way because the clients engine depends on internal `sync15`
/// structures, and can't be implemented as a syncable store...but `sync15`
/// doesn't know anything about multiple engines. This lets the sync manager
/// provide its own implementation for handling wipe and reset commands for all
/// the engines that it manages.
pub trait CommandProcessor {
    fn settings(&self) -> &Settings;

    /// Fetches commands to send to other clients. An error return value means
    /// commands couldn't be fetched, and halts the sync.
    fn fetch_outgoing_commands(&self) -> Result<HashSet<Command>>;

    /// Applies a command sent to this client from another client. This method
    /// should return a `CommandStatus` indicating whether the command was
    /// processed.
    ///
    /// An error return value means the sync manager encountered an error
    /// applying the command, and halts the sync to prevent unexpected behavior
    /// (for example, merging local and remote bookmarks, when we were told to
    /// wipe our local bookmarks).
    fn apply_incoming_command(&self, command: Command) -> Result<CommandStatus>;
}

/// Indicates if a command was applied successfully, ignored, or not supported.
/// Applied and ignored commands are removed from our client record, and never
/// retried. Unsupported commands are put back into our record, and retried on
/// subsequent syncs. This is to handle clients adding support for new data
/// types.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum CommandStatus {
    Applied,
    Ignored,
    Unsupported,
}

/// Information about this device to include in its client record. This should
/// be persisted across syncs, as part of the sync manager state.
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct Settings {
    /// The FxA device ID of this client, also used as this client's record ID
    /// in the clients collection.
    pub fxa_device_id: String,
    /// The name of this client. This should match the client's name in the
    /// FxA device manager.
    pub device_name: String,
    /// The type of this client: mobile, tablet, desktop, or other.
    pub device_type: DeviceType,
    /// The engines this client syncs. Not yet published.
    pub enabled_engines: Vec<String>,
}

#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum Command {
    /// Erases all local data for a specific engine.
    Wipe(String),
    /// Resets local sync state for all engines.
    ResetAll,
    /// Resets local sync state for a specific engine.
    Reset(String),
}
