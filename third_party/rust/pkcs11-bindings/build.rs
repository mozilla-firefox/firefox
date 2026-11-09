/* -*- Mode: rust; rust-indent-offset: 4 -*- */

extern crate bindgen;

use bindgen::callbacks::*;
use bindgen::*;

use std::env;
use std::path::PathBuf;

#[derive(Debug)]
struct PKCS11TypesParseCallbacks;

impl ParseCallbacks for PKCS11TypesParseCallbacks {
    fn int_macro(&self, name: &str, _value: i64) -> Option<IntKind> {
        if name == "CK_TRUE" || name == "CK_FALSE" {
            Some(IntKind::U8)
        } else {
            Some(IntKind::ULong)
        }
    }
}

fn main() {
    println!("cargo:rerun-if-changed=wrapper.h");

    let bindings = Builder::default()
        .header("wrapper.h")
        .allowlist_function("C_GetFunctionList")
        .allowlist_type("CK_C_INITIALIZE_ARGS_PTR")
        .allowlist_type("CK_CERTIFICATE_TYPE")
        .allowlist_type("CK_KEY_TYPE")
        .allowlist_type("CK_OBJECT_CLASS")
        .allowlist_type("CK_RSA_PKCS_PSS_PARAMS")
        .allowlist_type("CK_TRUST")
        .allowlist_var("CKP_PUBLIC_CERTIFICATES_TOKEN")
        .allowlist_var("CK_EFFECTIVELY_INFINITE")
        .allowlist_var("CK_FALSE")
        .allowlist_var("CK_INVALID_HANDLE")
        .allowlist_var("CK_TRUE")
        .allowlist_var("CK_UNAVAILABLE_INFORMATION")
        .allowlist_var("CKA_.*")
        .allowlist_var("CKC_.*")
        .allowlist_var("CKD_.*")
        .allowlist_var("CKF_.*")
        .allowlist_var("CKK_.*")
        .allowlist_var("CKM_.*")
        .allowlist_var("CKO_.*")
        .allowlist_var("CKR_.*")
        .allowlist_var("CKS_.*")
        .allowlist_var("CKT_.*")
        .derive_default(true)
        .parse_callbacks(Box::new(PKCS11TypesParseCallbacks))
        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR should be set in env"));
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
