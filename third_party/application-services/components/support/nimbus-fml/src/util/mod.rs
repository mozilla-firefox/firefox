/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{env, path::PathBuf};

pub mod loaders;

pub(crate) fn pkg_dir() -> String {
    env::var("CARGO_MANIFEST_DIR")
        .expect("Missing $CARGO_MANIFEST_DIR, cannot build tests for generated bindings")
}

pub(crate) fn join(base: String, suffix: &str) -> String {
    [base, suffix.to_string()]
        .iter()
        .collect::<PathBuf>()
        .to_string_lossy()
        .to_string()
}

// The Application Services directory
#[allow(dead_code)]
pub(crate) fn as_dir() -> String {
    join(pkg_dir(), "../../..")
}

// The Nimbus SDK directory
#[allow(dead_code)]
pub(crate) fn sdk_dir() -> String {
    join(as_dir(), "components/nimbus")
}

#[allow(dead_code)]
pub(crate) fn build_dir() -> String {
    join(pkg_dir(), "build")
}
