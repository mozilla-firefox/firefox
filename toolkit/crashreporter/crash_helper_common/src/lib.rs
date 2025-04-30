/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ffi::OsString;

pub mod errors;
pub mod messages;

mod breakpad;
mod ipc_channel;
mod ipc_connector;
mod ipc_listener;
mod ipc_poller;
mod platform;

use errors::MessageError;

// Re-export the platform-specific types and functions
pub use crate::breakpad::{AncillaryData, BreakpadChar, BreakpadData, BreakpadRawData, Pid};
pub use crate::ipc_channel::IPCChannel;
pub use crate::ipc_connector::{IPCConnector, IPCEvent};
pub use crate::ipc_listener::IPCListener;
pub use crate::ipc_poller::wait_for_events;

/// OsString extensions to convert from/to C strings. The strings will be
/// regular nul-terminated byte strings on most platforms but will use wide
/// characters instead on Windows.
pub trait BreakpadString {
    /// Turn an `OsString` into a vector of bytes
    fn serialize(&self) -> Vec<u8>;

    /// Reconstruct an `OsString` from a vector of bytes obtained by calling
    /// the `BreakpadString::serialize()` function.
    fn deserialize(bytes: &[u8]) -> Result<OsString, MessageError>;

    /// Create an OsString from a C nul-terminated string.
    ///
    /// # Safety
    ///
    /// The `ptr` argument must point to a valid nul-terminated C string.
    unsafe fn from_ptr(ptr: *const BreakpadChar) -> OsString;

    /// Create a nul-terminated C string holding the contents of this
    /// `OsString` object. The resulting pointer must be freed by retaking
    /// ownership of its memory via `BreakpadString::from_raw()`.
    fn into_raw(self) -> *mut BreakpadChar;

    /// Retake ownership of a nul-terminated C string created via a call to
    /// `BreakpadString::from_raw()`.
    ///
    /// # Safety
    ///
    /// The `ptr` argument must have been created via a call to the
    /// `BreakpadString::from_raw()` function.
    unsafe fn from_raw(ptr: *mut BreakpadChar) -> OsString;
}

pub const IO_TIMEOUT: u16 = 2 * 1000;
