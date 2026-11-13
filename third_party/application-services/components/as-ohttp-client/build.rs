/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

fn main() {
    uniffi::generate_scaffolding("./src/as_ohttp_client.udl").unwrap();

    // ohttp's build script (app-svc feature) detects NSS acceleration libraries
    // by file existence, but doesn't know the libraries added since NSS 3.125.
    if let Ok(nss_dir) = std::env::var("NSS_DIR") {
        println!("cargo:rerun-if-env-changed=NSS_DIR");
        let lib_dir = std::path::Path::new(&nss_dir).join("lib");
        for lib in &["pqcwrap_static", "crux", "aarch64-gcm-wrap_c_lib"] {
            if lib_dir.join(format!("lib{lib}.a")).is_file() {
                println!("cargo:rustc-link-lib=static={lib}");
            }
        }
    }
}
