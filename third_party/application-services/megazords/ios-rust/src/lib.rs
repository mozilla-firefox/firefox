/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(unknown_lints)]
#![warn(rust_2018_idioms)]

pub use ads_client;
pub use as_ohttp_client;
pub use autofill;
pub use context_id;
pub use crashtest;
pub use db_crypto;
pub use error_support;
pub use fxa_client;
pub use init_rust_components;
pub use logins;
pub use merino;
pub use nimbus;
pub use places;
pub use push;
pub use relay;
pub use remote_settings;
pub use rust_log_forwarder;
pub use search;
pub use suggest;
pub use sync15;
pub use sync_manager;
pub use tabs;
pub use tracing_support;
pub use viaduct;
#[cfg(target_vendor = "apple")]
pub use viaduct_backend_rust;
