// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

mod reexport_appservices_uniffi_scaffolding {
    ads_client::uniffi_reexport_scaffolding!();
    tabs::uniffi_reexport_scaffolding!();
    relevancy::uniffi_reexport_scaffolding!();
    suggest::uniffi_reexport_scaffolding!();
    webext_storage::uniffi_reexport_scaffolding!();
    search::uniffi_reexport_scaffolding!();
    context_id::uniffi_reexport_scaffolding!();
    filter_adult::uniffi_reexport_scaffolding!();
    error_support::uniffi_reexport_scaffolding!();
    tracing_support::uniffi_reexport_scaffolding!();
    logins::uniffi_reexport_scaffolding!();
    init_rust_components::uniffi_reexport_scaffolding!();
}

// Define extern "C" versions of these UniFFI functions, so that they can be called from C++
#[no_mangle]
pub extern "C" fn uniffi_rustbuffer_alloc(
    size: u64,
    call_status: &mut uniffi::RustCallStatus,
) -> uniffi::RustBuffer {
    uniffi::uniffi_rustbuffer_alloc(size, call_status)
}

#[no_mangle]
pub extern "C" fn uniffi_rustbuffer_free(
    buf: uniffi::RustBuffer,
    call_status: &mut uniffi::RustCallStatus,
) {
    uniffi::uniffi_rustbuffer_free(buf, call_status)
}
