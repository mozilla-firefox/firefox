/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#[cfg(target_os = "windows")]
pub(crate) mod windows;

#[cfg(any(target_os = "linux", target_os = "macos"))]
pub(crate) mod unix;

#[cfg(any(target_os = "android"))]
pub(crate) mod android;
