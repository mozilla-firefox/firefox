/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! This shouldn't exist, but does because if something isn't going to link
//! against `nss` but has an `nss`-enabled `sqlcipher` turned on (for example,
//! by a `cargo` feature activated by something else in the workspace).
//! it might need to issue link commands for NSS.

use std::{
    env,
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum LinkingKind {
    Dynamic { folded_libs: bool },
    Static,
}

#[derive(Debug, PartialEq, Eq, Clone)]
pub struct NoNssDir;

pub fn link_nss() -> Result<(), NoNssDir> {
    if env::var_os("MOZ_TOPOBJDIR").is_some() {
        mozbuild::link_nss();
        return Ok(());
    }
    let (lib_dir, include_dir) = get_nss()?;
    println!(
        "cargo:rustc-link-search=native={}",
        lib_dir.to_string_lossy()
    );
    println!("cargo:include={}", include_dir.to_string_lossy());
    let kind = determine_kind();
    link_nss_libs(kind, &lib_dir);
    Ok(())
}

pub fn link_nss_rustlib() -> Result<(), NoNssDir> {
    if env::var_os("MOZ_TOPOBJDIR").is_some() {
        mozbuild::link_nss_rustlib();
        return Ok(());
    }
    // The standalone NSS_DIR path already produces a self-contained link
    // (including mozpkix when statically linked), so this matches link_nss.
    link_nss()
}

fn get_nss() -> Result<(PathBuf, PathBuf), NoNssDir> {
    let nss_dir = env("NSS_DIR").ok_or(NoNssDir)?;
    let nss_dir = Path::new(&nss_dir);
    if !nss_dir.exists() {
        println!(
            "NSS_DIR path (obtained via `env`) does not exist: {}",
            nss_dir.display()
        );
        panic!("It looks like NSS is not built. Please run `libs/verify-[platform]-environment.sh` first!");
    }
    let lib_dir = nss_dir.join("lib");
    let include_dir = nss_dir.join("include");
    Ok((lib_dir, include_dir))
}

fn determine_kind() -> LinkingKind {
    if env_flag("NSS_STATIC") {
        LinkingKind::Static
    } else {
        let folded_libs = env_flag("NSS_USE_FOLDED_LIBS");
        LinkingKind::Dynamic { folded_libs }
    }
}

fn link_nss_libs(kind: LinkingKind, lib_dir: &Path) {
    let libs = get_nss_libs(kind, lib_dir);
    // Emit -L flags
    let kind_str = match kind {
        LinkingKind::Dynamic { .. } => "dylib",
        LinkingKind::Static => "static",
    };
    for lib in libs {
        println!("cargo:rustc-link-lib={}={}", kind_str, lib);
    }
    // Link against C++ stdlib (for mozpkix)
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    if target_os == "android" || target_os == "linux" {
        println!("cargo:rustc-link-lib=stdc++");
    } else {
        println!("cargo:rustc-link-lib=c++");
    }
    let target_arch = env::var("CARGO_CFG_TARGET_ARCH").unwrap();
    if target_arch == "x86_64" && target_os == "android" {
        let android_home = env::var("ANDROID_HOME").expect("ANDROID_HOME not set");
        const ANDROID_NDK_VERSION: &str = "30.0.16248370";
        // One of these will exist, depending on the host platform.
        const DARWIN_X86_64_LIB_DIR: &str =
            "/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/21/lib/linux/";
        println!("cargo:rustc-link-search={android_home}/ndk/{ANDROID_NDK_VERSION}/{DARWIN_X86_64_LIB_DIR}");
        const LINUX_X86_64_LIB_DIR: &str =
            "/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/21/lib/linux/";
        println!("cargo:rustc-link-search={android_home}/ndk/{ANDROID_NDK_VERSION}/{LINUX_X86_64_LIB_DIR}");
        println!("cargo:rustc-link-lib=static=clang_rt.builtins-x86_64-android");
    }
}

fn get_nss_libs(kind: LinkingKind, lib_dir: &Path) -> Vec<String> {
    match kind {
        LinkingKind::Static => {
            // rc_crypto verifies content signatures with mozpkix, which no .pc names.
            let mut static_libs = vec![String::from("mozpkix")];
            static_libs.extend(
                pkg_config_libs(lib_dir)
                    .into_iter()
                    // libsqlite3-sys supplies SQLite.
                    .filter(|lib| lib != "sqlite"),
            );
            static_libs
        }
        LinkingKind::Dynamic { folded_libs } => {
            let mut dylibs = vec!["freebl3", "nss3", "nssckbi", "softokn3"];
            if !folded_libs {
                dylibs.extend(["nspr4", "nssutil3", "plc4", "plds4"]);
            }
            dylibs.into_iter().map(String::from).collect()
        }
    }
}

/// The `-l` names on the `Libs:` lines of the `nss-static.pc` and `nspr.pc` that
/// `libs/copy-nss-libs.sh` copies from NSS's `build.sh --static` dist.
fn pkg_config_libs(lib_dir: &Path) -> Vec<String> {
    let mut libs = Vec::new();
    for pc in ["nss-static.pc", "nspr.pc"] {
        let path = lib_dir.join("pkgconfig").join(pc);
        println!("cargo:rerun-if-changed={}", path.display());
        let text = fs::read_to_string(&path).unwrap_or_else(|e| {
            // The build scripts skip a dist that exists, so an older one must be deleted.
            panic!(
                "could not read {}: {e}. This NSS was probably built by an older version of \
                 libs/; delete {} and rebuild it with `cd libs && ./build-all.sh <platform>`",
                path.display(),
                lib_dir.parent().unwrap_or(lib_dir).display()
            )
        });
        libs.extend(
            text.lines()
                .filter_map(|line| line.strip_prefix("Libs:"))
                .flat_map(str::split_whitespace)
                .filter_map(|flag| flag.strip_prefix("-l"))
                .map(String::from),
        );
    }
    libs
}

pub fn env(name: &str) -> Option<OsString> {
    println!("cargo:rerun-if-env-changed={}", name);
    env::var_os(name)
}

pub fn env_str(name: &str) -> Option<String> {
    println!("cargo:rerun-if-env-changed={}", name);
    env::var(name).ok()
}

pub fn env_flag(name: &str) -> bool {
    match env_str(name).as_ref().map(String::as_ref) {
        Some("1") => true,
        Some("0") => false,
        Some(s) => {
            println!(
                "cargo:warning=unknown value for environment var {:?}: {:?}. Ignoring",
                name, s
            );
            false
        }
        None => false,
    }
}
