/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(unknown_lints)]
#![warn(rust_2018_idioms)]

mod error;
mod interruptee;
mod shutdown;
mod sql;

pub use error::Interrupted;
pub use interruptee::*;
pub use shutdown::*;
pub use sql::*;

uniffi::include_scaffolding!("interrupt_support");
