/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*****************************************************************************
 * Windows                                                                   *
 *****************************************************************************/

#[cfg(target_os = "windows")]
pub use windows::wait_for_events;

#[cfg(target_os = "windows")]
pub(crate) mod windows;

/*****************************************************************************
 * Android, macOS & Linux                                                    *
 *****************************************************************************/

#[cfg(any(target_os = "android", target_os = "linux", target_os = "macos"))]
pub use unix::wait_for_events;

#[cfg(any(target_os = "android", target_os = "linux", target_os = "macos"))]
pub(crate) mod unix;
