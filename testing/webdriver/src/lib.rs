#![allow(non_snake_case)]
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![forbid(unsafe_code)]

extern crate base64;
extern crate cookie;
extern crate icu_segmenter;
#[macro_use]
extern crate log;
extern crate http;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;
extern crate time;
#[cfg(feature = "server")]
extern crate tokio;
extern crate url;
#[cfg(feature = "server")]
extern crate warp;

#[macro_use]
pub mod macros;
pub mod actions;
pub mod capabilities;
pub mod command;
pub mod common;
pub mod error;
pub mod httpapi;
pub mod response;
#[cfg(feature = "server")]
pub mod server;

#[cfg(test)]
pub mod test;

pub use common::Parameters;
