/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub fn main() {
    #[cfg(feature = "uniffi-bindings")]
    uniffi::generate_scaffolding("./src/fml.udl").unwrap();
}
