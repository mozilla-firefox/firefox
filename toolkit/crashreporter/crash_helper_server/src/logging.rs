/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#[cfg(target_os = "android")]
pub(crate) use android::init;

#[cfg(target_os = "android")]
pub(crate) mod android;

#[cfg(not(target_os = "android"))]
pub(crate) use env::init;

#[cfg(not(target_os = "android"))]
pub(crate) mod env;
