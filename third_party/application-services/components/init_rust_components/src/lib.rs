/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#[cfg(feature = "ohttp")]
use viaduct::ohttp::configure_default_ohttp_channels;
uniffi::setup_scaffolding!();

/// Global initialization routines for Rust components. Must be called before any other calls to
/// Rust components.
///
/// For adding additional initialization code: Note that this function is called very early in the
/// app lifetime and therefore affects the startup time. Only the most necessary things should be
/// done here.
#[cfg(not(feature = "keydb"))]
#[uniffi::export]
pub fn initialize() {
    error_support::initialize();

    #[cfg(feature = "nss")]
    nss_as::ensure_initialized();

    #[cfg(feature = "ohttp")]
    {
        configure_default_ohttp_channels()
            .expect("We pass down hard coded Strings for the relays, if this fails, we have a typo in the config we pass down.");
    }
}

/// Global initialization routines for Rust components, when `logins/keydb` feature is activated. Must be
/// called before any other calls to Rust components.
///
/// Receives the path to the profile directory.
///
/// For adding additional initialization code: Note that this function is called very early in the
/// app lifetime and therefore affects the startup time. Only the most necessary things should be
/// done here.
#[cfg(feature = "keydb")]
#[uniffi::export]
pub fn initialize(profile_path: String) {
    nss_as::ensure_initialized_with_profile_dir(profile_path);

    #[cfg(feature = "ohttp")]
    {
        configure_default_ohttp_channels()
            .expect("We pass down hard coded Strings for the relays, if this fails, we have a typo in the config we pass down.");
    }
}
