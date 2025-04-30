/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use rustc_version::{version, Version};

fn main() {
    let mut build = cc::Build::new();
    build.cpp(true);
    // For js-confdefs.h, see wrappers.cpp.
    build.include(mozbuild::TOPOBJDIR.join("js").join("src"));
    build.include(mozbuild::TOPOBJDIR.join("dist").join("include"));
    build.define("MOZ_HAS_MOZGLUE", None);
    build.file("wrappers.cpp");
    build.compile("wrappers");
    println!("cargo:rerun-if-changed=wrappers.cpp");

    let ver = version().unwrap();
    let max_oom_hook_version = Version::parse("1.89.0-alpha").unwrap();
    // The new alloc error panic feature was temporarily reverted. We kept the
    // code in tree, but the version here is such that it's effectively never used.
    let max_alloc_error_panic_version = Version::parse("1.89.0-alpha").unwrap();

    println!("cargo::rustc-check-cfg=cfg(has_panic_hook_info)");
    println!("cargo::rustc-check-cfg=cfg(oom_with, values(\"hook\", \"alloc_error_panic\"))");
    if ver < max_oom_hook_version {
        println!("cargo:rustc-cfg=oom_with=\"hook\"");
    } else if ver < max_alloc_error_panic_version {
        println!("cargo:rustc-cfg=oom_with=\"alloc_error_panic\"");
    } else if std::env::var("MOZ_AUTOMATION").is_ok() {
        panic!("Builds on automation must use a version of rust for which we know how to hook OOM: want < {}, have {}",
               max_alloc_error_panic_version, ver);
    }
}
