// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::cell::RefCell;

use nss::{
    Mode, RecordProtection as Aead, RecordProtectionOps as _, TLS_AES_128_GCM_SHA256,
    TLS_VERSION_1_3, hkdf,
};
use tracing::error;

use crate::{Error, Res, version::Version};

/// The AEAD used for Retry is fixed, so use thread local storage.
fn make_aead(version: Version, mode: Mode) -> Aead {
    #[cfg(debug_assertions)]
    ::nss::assert_initialized();

    let secret = hkdf::import_key(TLS_VERSION_1_3, version.retry_secret()).expect("can import key");
    Aead::new(
        TLS_VERSION_1_3,
        TLS_AES_128_GCM_SHA256,
        &secret,
        version.label_prefix(),
        mode,
    )
    .expect("can create AEAD")
}
#[cfg(feature = "draft-29")]
thread_local!(static RETRY_AEAD_29_ENC: RefCell<Aead> = RefCell::new(make_aead(Version::Draft29, Mode::Encrypt)));
#[cfg(feature = "draft-29")]
thread_local!(static RETRY_AEAD_29_DEC: RefCell<Aead> = RefCell::new(make_aead(Version::Draft29, Mode::Decrypt)));
thread_local!(static RETRY_AEAD_V1_ENC: RefCell<Aead> = RefCell::new(make_aead(Version::Version1, Mode::Encrypt)));
thread_local!(static RETRY_AEAD_V1_DEC: RefCell<Aead> = RefCell::new(make_aead(Version::Version1, Mode::Decrypt)));
thread_local!(static RETRY_AEAD_V2_ENC: RefCell<Aead> = RefCell::new(make_aead(Version::Version2, Mode::Encrypt)));
thread_local!(static RETRY_AEAD_V2_DEC: RefCell<Aead> = RefCell::new(make_aead(Version::Version2, Mode::Decrypt)));

/// Run a function with the appropriate Retry AEAD.
pub fn use_aead<F, T>(version: Version, mode: Mode, f: F) -> Res<T>
where
    F: FnOnce(&Aead) -> Res<T>,
{
    match (version, mode) {
        (Version::Version2, Mode::Encrypt) => &RETRY_AEAD_V2_ENC,
        (Version::Version2, Mode::Decrypt) => &RETRY_AEAD_V2_DEC,
        (Version::Version1, Mode::Encrypt) => &RETRY_AEAD_V1_ENC,
        (Version::Version1, Mode::Decrypt) => &RETRY_AEAD_V1_DEC,
        #[cfg(feature = "draft-29")]
        (Version::Draft29, Mode::Encrypt) => &RETRY_AEAD_29_ENC,
        #[cfg(feature = "draft-29")]
        (Version::Draft29, Mode::Decrypt) => &RETRY_AEAD_29_DEC,
    }
    .try_with(|aead| f(&aead.borrow()))
    .map_err(|e| {
        error!("Unable to access Retry AEAD: {e:?}");
        Error::Internal
    })?
}

/// Determine how large the expansion is for a given key.
pub fn expansion(version: Version) -> usize {
    use_aead(version, Mode::Encrypt, |aead| Ok(aead.expansion()))
        .expect("Unable to access Retry AEAD")
}
