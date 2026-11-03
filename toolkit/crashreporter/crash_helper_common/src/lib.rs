/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ffi::OsString;

pub mod errors;
pub mod messages;

mod appinfo;
mod breakpad;
mod ipc_channel;
mod ipc_connector;
mod ipc_listener;
mod ipc_queue;
mod platform;

pub mod crash_annotations {
    include!(concat!(env!("OUT_DIR"), "/crash_annotations.rs"));
}

use messages::MessageError;
use mozannotation_server::CAnnotation;

// Matches the same type in mozglue/misc/ProcessType.h
pub type GeckoChildId = i32;

// Re-export the platform-specific types and functions
pub use crate::appinfo::ApplicationInfo;
pub use crate::breakpad::{BreakpadChar, BreakpadData, BreakpadRawData, Pid};
pub use crate::ipc_channel::{IPCChannel, IPCClientChannel};
pub use crate::ipc_connector::{
    AncillaryData, IPCConnector, IPCConnectorKey, IPCEvent, RawIPCConnector,
};
pub use crate::ipc_listener::{IPCListener, IPCListenerError};
pub use crate::ipc_queue::IPCQueue;
pub use crate::platform::{
    AsRawProcessHandle, AsRawThreadHandle, FromRawProcessHandle, FromRawThreadHandle,
    PlatformError, ProcessHandle, RawProcessHandle, RawThreadHandle, ThreadHandle,
};

#[cfg(target_os = "windows")]
pub use crate::platform::server_addr;

#[cfg(any(target_os = "macos", target_os = "ios"))]
pub use crate::platform::{
    mach_msg_recv, mach_msg_send, AsRawPort, MachMessageWrapper, MachPortRight, ReceiveRight,
    SendRight, SendRightRef,
};

/// OsString extensions to convert from/to C strings. The strings will be
/// regular nul-terminated byte strings on most platforms but will use wide
/// characters instead on Windows.
pub trait BreakpadString {
    /// Turn an `OsString` into a vector of bytes
    fn serialize(self) -> Vec<u8>;

    /// Reconstruct an `OsString` from a vector of bytes obtained by calling
    /// the `BreakpadString::serialize()` function.
    fn deserialize(bytes: Vec<u8>) -> Result<OsString, MessageError>;

    /// Return the number of bytes that are required to store the contents of
    /// this string. This will be the same size as the array returned by the
    /// `serialize()` function.
    fn bytes_len(&self) -> usize;

    /// Return the number of characters in this string excluding the null
    /// terminator.
    fn len(&self) -> usize;

    /// Returns true if the string is empty.
    fn is_empty(&self) -> bool {
        self.len() == 0
    }

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

// Use a longer timeout for I/O operations on Android as the order in which
// processes are started is different than on the desktop platforms.
#[cfg(target_os = "android")]
pub const IO_TIMEOUT: u16 = 5 * 1000;
#[cfg(not(target_os = "android"))]
pub const IO_TIMEOUT: u16 = 2 * 1000;

/// Payload that can be passed through the minidump writer to the finalizing callback.
/// It will typically be weaved through FFIs via opaque pointers.
#[derive(Default)]
pub struct ExtraCrashData {
    pub error: Option<std::ffi::CString>,
    pub annotations: Vec<CAnnotation>,
}
