// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

extern crate geckoservo;

extern crate app_services_logger;
#[cfg(feature = "cubeb-remoting")]
extern crate audioipc2_client;
#[cfg(feature = "cubeb-remoting")]
extern crate audioipc2_server;
extern crate authrs_bridge;
#[cfg(feature = "bitsdownload")]
extern crate bitsdownload;
#[cfg(feature = "moz_places")]
extern crate bookmark_sync;
extern crate buildid_reader_ffi;
extern crate cascade_bloom_filter;
extern crate cert_storage;
extern crate chardetng_c;
extern crate cosec;
extern crate crypto_hash;
#[cfg(feature = "cubeb_coreaudio_rust")]
extern crate cubeb_coreaudio;
#[cfg(feature = "cubeb_pulse_rust")]
extern crate cubeb_pulse;
extern crate data_storage;
extern crate dom_fragmentdirectives;
extern crate encoding_glue;
extern crate fog_control;
extern crate gecko_profiler;
extern crate gkrust_utils;
extern crate http_sfv;
extern crate idna_glue;
extern crate ipdl_utils;
extern crate jog;
extern crate jsrust_shared;
extern crate kvstore;
extern crate mapped_hyph;
extern crate mozurl;
extern crate mp4parse_capi;
extern crate netwerk_helper;
extern crate nserror;
extern crate nsstring;
extern crate prefs_parser;
extern crate processtools;
#[cfg(feature = "gecko_profiler")]
extern crate profiler_helper;
extern crate rsdparsa_capi;
extern crate signature_cache;
extern crate static_prefs;
extern crate storage;
extern crate webrender_bindings;
extern crate xpcom;

extern crate audio_thread_priority;

#[cfg(not(target_os = "android"))]
extern crate webext_storage;

#[cfg(feature = "webrtc")]
extern crate mdns_service;
extern crate neqo_glue;
extern crate wgpu_bindings;

extern crate aa_stroke;
extern crate qcms;
extern crate wpf_gpu_raster;

extern crate unic_langid;
extern crate unic_langid_ffi;

extern crate fluent_langneg;
extern crate fluent_langneg_ffi;

extern crate fluent;
extern crate fluent_ffi;

extern crate oxilangtag_ffi;
extern crate unicode_bidi_ffi;

extern crate rure;

extern crate fluent_fallback;
extern crate l10nregistry_ffi;
extern crate localization_ffi;

extern crate ipcclientcerts;
extern crate trust_anchors;

#[cfg(any(
    target_os = "android",
    target_os = "macos",
    target_os = "ios",
    all(target_os = "windows", not(target_arch = "aarch64"))
))]
extern crate osclientcerts;

#[cfg(not(target_os = "android"))]
extern crate gkrust_uniffi_components;

#[cfg(feature = "uniffi_fixtures")]
extern crate gkrust_uniffi_fixtures;

#[cfg(not(target_os = "android"))]
extern crate viaduct;

extern crate gecko_logger;

#[cfg(feature = "oxidized_breakpad")]
extern crate rust_minidump_writer_linux;

#[cfg(feature = "crashreporter")]
extern crate mozannotation_client;

#[cfg(feature = "crashreporter")]
extern crate crash_helper_client;

#[cfg(feature = "webmidi_midir_impl")]
extern crate midir_impl;

#[cfg(target_os = "windows")]
extern crate detect_win32k_conflicts;

extern crate origin_trials_ffi;

extern crate dap_ffi;

extern crate data_encoding_ffi;

extern crate binary_http;
extern crate mls_gk;
extern crate oblivious_http;

extern crate mime_guess_ffi;

#[cfg(feature = "libz-rs-sys")]
extern crate libz_rs_sys;

extern crate log;
use log::info;

use std::{ffi::CStr, os::raw::c_char};

use gecko_logger::GeckoLogger;

#[no_mangle]
pub extern "C" fn GkRust_Init() {
    // Initialize logging.
    let _ = GeckoLogger::init();
}

#[no_mangle]
pub extern "C" fn GkRust_Shutdown() {}

/// Used to implement `nsIDebug2::RustPanic` for testing purposes.
#[no_mangle]
pub unsafe extern "C" fn intentional_panic(message: *const c_char) {
    panic!("{}", CStr::from_ptr(message).to_string_lossy());
}

/// Used to implement `nsIDebug2::rustLog` for testing purposes.
#[no_mangle]
pub unsafe extern "C" fn debug_log(target: *const c_char, message: *const c_char) {
    // NOTE: The `info!` log macro is used here because we have the `release_max_level_info` feature set.
    info!(target: CStr::from_ptr(target).to_str().unwrap(), "{}", CStr::from_ptr(message).to_str().unwrap());
}
