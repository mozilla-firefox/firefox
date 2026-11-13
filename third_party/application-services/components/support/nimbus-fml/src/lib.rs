/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod backends;
pub mod command_line;
pub(crate) mod defaults;
mod editing;
pub mod error;
pub(crate) mod frontend;
pub mod intermediate_representation;
pub mod lints;
pub mod parser;
pub(crate) mod schema;
pub mod util;

cfg_if::cfg_if! {
    if #[cfg(feature = "client-lib")] {
        pub mod client;
        pub use crate::client::*;
    }
}

#[cfg(test)]
pub mod fixtures;

const SUPPORT_URL_LOADING: bool = true;

#[cfg(feature = "uniffi-bindings")]
uniffi::custom_type!(JsonObject, String, {
    remote,
    try_lift: |val| {
        let json: serde_json::Value = serde_json::from_str(&val)?;

        match json.as_object() {
            Some(obj) => Ok(obj.to_owned()),
            _ => Err(uniffi::deps::anyhow::anyhow!(
                "Unexpected JSON-non-object in the bagging area"
            )),
        }
    },
    lower: |obj| serde_json::Value::Object(obj).to_string(),
});

#[cfg(feature = "uniffi-bindings")]
use url::Url;

#[cfg(feature = "uniffi-bindings")]
uniffi::custom_type!(Url, String, {
    remote,
    try_lift: |val| Ok(Url::parse(&val)?),
    lower: |obj| obj.as_str().to_string(),
});
