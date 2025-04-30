/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#[cfg(target_os = "windows")]
pub use windows::{AncillaryData, BreakpadChar, BreakpadData, BreakpadRawData, Pid};
#[cfg(target_os = "windows")]
pub(crate) mod windows;

#[cfg(any(target_os = "android", target_os = "linux"))]
pub use linux::{AncillaryData, BreakpadChar, BreakpadData, BreakpadRawData, Pid};
#[cfg(any(target_os = "android", target_os = "linux"))]
pub(crate) mod linux;

#[cfg(target_os = "macos")]
pub use macos::{AncillaryData, BreakpadChar, BreakpadData, BreakpadRawData, Pid};
#[cfg(target_os = "macos")]
pub(crate) mod macos;

#[cfg(any(target_os = "android", target_os = "linux", target_os = "macos"))]
mod unix_strings;
#[cfg(target_os = "windows")]
mod windows_strings;
