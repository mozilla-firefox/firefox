fn main() {
    if let Some(true) = version_check::supports_feature("doc_cfg") {
        println!("cargo:rustc-cfg=nightly");
    }
}
