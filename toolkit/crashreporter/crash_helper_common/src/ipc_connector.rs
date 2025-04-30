/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use crate::messages::Header;

pub enum IPCEvent {
    Connect(IPCConnector),
    Header(usize, Header),
    Disconnect(usize),
}

/*****************************************************************************
 * Windows                                                                   *
 *****************************************************************************/

#[cfg(target_os = "windows")]
pub use windows::IPCConnector;

#[cfg(target_os = "windows")]
pub(crate) mod windows;

/*****************************************************************************
 * Android, macOS & Linux                                                    *
 *****************************************************************************/

#[cfg(any(target_os = "android", target_os = "linux", target_os = "macos"))]
pub use unix::IPCConnector;

#[cfg(any(target_os = "android", target_os = "linux", target_os = "macos"))]
pub(crate) mod unix;
