/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// This is the *local* encryption support - it has nothing to do with the
// encryption used by sync.

// For context, what "local encryption" means in this context is:
// * We use regular sqlite, but ensure that sensitive data is encrypted in the DB in the
//   `secure_fields` column.  The encryption key is managed by the app.
// * The `decrypt_struct` and `encrypt_struct` functions are used to convert between an encrypted
//   `secure_fields` string and a decrypted `SecureFields` struct
// * Most API functions return `EncryptedLogin` which has its data encrypted.
//
// This makes life tricky for Sync - sync has its own encryption and its own
// management of sync keys. The entire records are encrypted on the server -
// so the record on the server has the plain-text data (which is then
// encrypted as part of the entire record), so:
// * When transforming a record from the DB into a Sync record, we need to
//   *decrypt* the data.
// * When transforming a record from Sync into a DB record, we need to *encrypt*
//   the data.
//
// So Sync needs to know the key etc, and that needs to get passed down
// multiple layers, from the app saying "sync now" all the way down to the
// low level sync code.
// To make life a little easier, we do that via a struct.
//
// Consumers of the Login component have 3 options for setting up encryption:
//    1. Implement EncryptorDecryptor directly
//       eg `LoginStore::new(MyEncryptorDecryptor)`
//    2. Implement KeyManager and use ManagedEncryptorDecryptor
//       eg `LoginStore::new(ManagedEncryptorDecryptor::new(MyKeyManager))`
//    3. Generate a single key and create a StaticKeyManager and use it together with
//       ManagedEncryptorDecryptor
//       eg `LoginStore::new(ManagedEncryptorDecryptor::new(StaticKeyManager { key: myKey }))`
//
//  You can implement EncryptorDecryptor directly to keep full control over the encryption
//  algorithm. For example, on the desktop, this could make use of NSS's SecretDecoderRing to
//  achieve transparent key management.
//
//  If the application wants to keep the current encryption, like Android and iOS, for example, but
//  control the key management itself, the KeyManager can be implemented and the encryption can be
//  done on the Rust side with the ManagedEncryptorDecryptor.
//
//  In tests or some command line tools, it can be practical to use a static key that does not
//  change at runtime and is already present when the LoginsStore is initialized. In this case, it
//  makes sense to use the provided StaticKeyManager.

// work around not yet having https://github.com/mozilla/uniffi-rs/pull/2963.
#![allow(const_evaluatable_unchecked)]

use crate::error::*;
use std::sync::Arc;

#[cfg(feature = "keydb")]
use futures::executor::block_on;

#[cfg(feature = "keydb")]
use async_trait::async_trait;

#[cfg(feature = "keydb")]
use parking_lot::RwLock;

#[cfg(feature = "keydb")]
use nss_as::assert_initialized as assert_nss_initialized;
#[cfg(feature = "keydb")]
use nss_as::pk11::sym_key::{
    authenticate_with_primary_password, authentication_with_primary_password_is_needed,
    get_or_create_aes256_key,
};

/// This is the generic EncryptorDecryptor trait, as handed over to the Store during initialization.
/// Consumers can implement either this generic trait and bring in their own crypto, or leverage the
/// ManagedEncryptorDecryptor below, which provides encryption algorithms out of the box.
///
/// Note that EncryptorDecryptor must not call any LoginStore methods. The login store can call out
/// to the EncryptorDecryptor when it's internal mutex is held so calling back in to the LoginStore
/// may deadlock.
#[uniffi::trait_interface]
pub trait EncryptorDecryptor: Send + Sync {
    fn encrypt(&self, cleartext: Vec<u8>) -> ApiResult<Vec<u8>>;
    fn decrypt(&self, ciphertext: Vec<u8>) -> ApiResult<Vec<u8>>;
}

impl<T: EncryptorDecryptor> EncryptorDecryptor for Arc<T> {
    fn encrypt(&self, clearbytes: Vec<u8>) -> ApiResult<Vec<u8>> {
        (**self).encrypt(clearbytes)
    }

    fn decrypt(&self, cipherbytes: Vec<u8>) -> ApiResult<Vec<u8>> {
        (**self).decrypt(cipherbytes)
    }
}

/// The ManagedEncryptorDecryptor makes use of the NSS provided cryptographic algorithms. The
/// ManagedEncryptorDecryptor uses a KeyManager for encryption key retrieval.
pub struct ManagedEncryptorDecryptor {
    key_manager: Arc<dyn KeyManager>,
}

impl ManagedEncryptorDecryptor {
    #[uniffi::constructor()]
    pub fn new(key_manager: Arc<dyn KeyManager>) -> Self {
        Self { key_manager }
    }
}

impl EncryptorDecryptor for ManagedEncryptorDecryptor {
    fn encrypt(&self, clearbytes: Vec<u8>) -> ApiResult<Vec<u8>> {
        let keybytes = self
            .key_manager
            .get_key()
            .map_err(|_| LoginsApiError::MissingKey)?;
        let key = std::str::from_utf8(&keybytes).map_err(|_| LoginsApiError::InvalidKey)?;

        let encdec = jwcrypto::EncryptorDecryptor::new(key)
            .map_err(|_: jwcrypto::JwCryptoError| LoginsApiError::InvalidKey)?;

        let cleartext =
            std::str::from_utf8(&clearbytes).map_err(|e| LoginsApiError::EncryptionFailed {
                reason: e.to_string(),
            })?;
        encdec
            .encrypt(cleartext)
            .map_err(
                |e: jwcrypto::JwCryptoError| LoginsApiError::EncryptionFailed {
                    reason: e.to_string(),
                },
            )
            .map(|text| text.into())
    }

    fn decrypt(&self, cipherbytes: Vec<u8>) -> ApiResult<Vec<u8>> {
        let keybytes = self
            .key_manager
            .get_key()
            .map_err(|_| LoginsApiError::MissingKey)?;
        let key = std::str::from_utf8(&keybytes).map_err(|_| LoginsApiError::InvalidKey)?;

        let encdec = jwcrypto::EncryptorDecryptor::new(key)
            .map_err(|_: jwcrypto::JwCryptoError| LoginsApiError::InvalidKey)?;

        let ciphertext =
            std::str::from_utf8(&cipherbytes).map_err(|e| LoginsApiError::DecryptionFailed {
                reason: e.to_string(),
            })?;
        encdec
            .decrypt(ciphertext)
            .map_err(
                |e: jwcrypto::JwCryptoError| LoginsApiError::DecryptionFailed {
                    reason: e.to_string(),
                },
            )
            .map(|text| text.into())
    }
}

/// Consumers can implement the KeyManager in combination with the ManagedEncryptorDecryptor to hand
/// over the encryption key whenever encryption or decryption happens.
#[uniffi::trait_interface]
pub trait KeyManager: Send + Sync {
    fn get_key(&self) -> ApiResult<Vec<u8>>;
}

/// Last but not least we provide a StaticKeyManager, which can be
/// used in cases where there is a single key during runtime, for example in tests.
pub struct StaticKeyManager {
    key: String,
}

impl StaticKeyManager {
    pub fn new(key: String) -> Self {
        Self { key }
    }
}

impl KeyManager for StaticKeyManager {
    #[handle_error(Error)]
    fn get_key(&self) -> ApiResult<Vec<u8>> {
        Ok(self.key.as_bytes().into())
    }
}

/// `PrimaryPasswordAuthenticator` is used in conjunction with `NSSKeyManager` to provide the
/// primary password and the success or failure actions of the authentication process.
#[cfg(feature = "keydb")]
#[uniffi::export(with_foreign)]
#[async_trait]
pub trait PrimaryPasswordAuthenticator: Send + Sync {
    /// Get a primary password for authentication, otherwise return the
    /// AuthenticationCancelled error to cancel the authentication process.
    async fn get_primary_password(&self) -> ApiResult<String>;
    async fn on_authentication_success(&self) -> ApiResult<()>;
    async fn on_authentication_failure(&self) -> ApiResult<()>;
}

/// Use the `NSSKeyManager` to use NSS for key management.
///
/// NSS stores keys in `key4.db` within the profile and wraps the key with a key derived from the
/// primary password, if set. It defers to the provided `PrimaryPasswordAuthenticator`
/// implementation to handle user authentication.  Note that if no primary password is set, the
/// wrapping key is deterministically derived from an empty string.
///
/// Make sure to initialize NSS using `ensure_initialized_with_profile_dir` before creating a
/// NSSKeyManager.
///
/// The key is cached after the first retrieval, since fetching it from NSS costs at least one
/// token round-trip. The cache is dropped whenever the token turns out to be locked again.
///
/// # Examples
/// ```no_run
/// use async_trait::async_trait;
/// use logins::encryption::KeyManager;
/// use logins::{PrimaryPasswordAuthenticator, LoginsApiError, NSSKeyManager};
/// use std::sync::Arc;
///
/// struct MyPrimaryPasswordAuthenticator {}
///
/// #[async_trait]
/// impl PrimaryPasswordAuthenticator for MyPrimaryPasswordAuthenticator {
///     async fn get_primary_password(&self) -> Result<String, LoginsApiError> {
///         // Most likely, you would want to prompt for a password.
///         // let password = prompt_string("primary password").unwrap_or_default();
///         Ok("secret".to_string())
///     }
///
///     async fn on_authentication_success(&self) -> Result<(), LoginsApiError> {
///         println!("success");
///         Ok(())
///     }
///
///     async fn on_authentication_failure(&self) -> Result<(), LoginsApiError> {
///         println!("this did not work, please try again:");
///         Ok(())
///     }
/// }
/// let key_manager = NSSKeyManager::new(Arc::new(MyPrimaryPasswordAuthenticator {}));
/// assert_eq!(key_manager.get_key().unwrap().len(), 63);
/// ```
#[cfg(feature = "keydb")]
#[derive(uniffi::Object)]
pub struct NSSKeyManager {
    primary_password_authenticator: Arc<dyn PrimaryPasswordAuthenticator>,
    cached_key: RwLock<Option<Vec<u8>>>,
}

#[cfg(feature = "keydb")]
#[uniffi::export]
impl NSSKeyManager {
    /// Initialize new `NSSKeyManager` with a given `PrimaryPasswordAuthenticator`.
    /// There must be a previous initializiation of NSS before initializing
    /// `NSSKeyManager`, otherwise this panics.
    #[uniffi::constructor()]
    pub fn new(primary_password_authenticator: Arc<dyn PrimaryPasswordAuthenticator>) -> Self {
        assert_nss_initialized();
        Self {
            primary_password_authenticator,
            cached_key: RwLock::new(None),
        }
    }

    pub fn into_dyn_key_manager(self: Arc<Self>) -> Arc<dyn KeyManager> {
        self
    }
}

/// Identifier for the logins key, under which the key is stored in NSS.
#[cfg(feature = "keydb")]
static KEY_NAME: &str = "as-logins-key";

// wrapp `authentication_with_primary_password_is_needed` into an ApiResult
#[cfg(feature = "keydb")]
fn api_authentication_with_primary_password_is_needed() -> ApiResult<bool> {
    authentication_with_primary_password_is_needed().map_err(|e: nss_as::Error| {
        LoginsApiError::NSSAuthenticationError {
            reason: e.to_string(),
        }
    })
}

// wrapp `authenticate_with_primary_password` into an ApiResult
#[cfg(feature = "keydb")]
fn api_authenticate_with_primary_password(primary_password: &str) -> ApiResult<bool> {
    authenticate_with_primary_password(primary_password).map_err(|e: nss_as::Error| {
        LoginsApiError::NSSAuthenticationError {
            reason: e.to_string(),
        }
    })
}

#[cfg(feature = "keydb")]
impl KeyManager for NSSKeyManager {
    fn get_key(&self) -> ApiResult<Vec<u8>> {
        if api_authentication_with_primary_password_is_needed()? {
            // The token locked again since we cached the key, so the cached copy must go.
            *self.cached_key.write() = None;

            let primary_password =
                block_on(self.primary_password_authenticator.get_primary_password())?;
            let mut result = api_authenticate_with_primary_password(&primary_password)?;

            if result {
                block_on(
                    self.primary_password_authenticator
                        .on_authentication_success(),
                )?;
            } else {
                while !result {
                    block_on(
                        self.primary_password_authenticator
                            .on_authentication_failure(),
                    )?;

                    let primary_password =
                        block_on(self.primary_password_authenticator.get_primary_password())?;
                    result = api_authenticate_with_primary_password(&primary_password)?;
                }
                block_on(
                    self.primary_password_authenticator
                        .on_authentication_success(),
                )?;
            }
        }

        let cached = self.cached_key.read().clone();
        if let Some(bytes) = cached {
            return Ok(bytes);
        }

        let key = get_or_create_aes256_key(KEY_NAME).map_err(|_| LoginsApiError::MissingKey)?;
        let mut bytes: Vec<u8> = Vec::new();
        serde_json::to_writer(
            &mut bytes,
            &jwcrypto::Jwk::new_direct_from_bytes(None, &key),
        )
        .unwrap();
        *self.cached_key.write() = Some(bytes.clone());
        Ok(bytes)
    }
}

#[handle_error(Error)]
pub fn create_canary(text: &str, key: &str) -> ApiResult<String> {
    Ok(jwcrypto::EncryptorDecryptor::new(key)?.create_canary(text)?)
}

pub fn check_canary(canary: &str, text: &str, key: &str) -> ApiResult<bool> {
    let encdec = jwcrypto::EncryptorDecryptor::new(key)
        .map_err(|_: jwcrypto::JwCryptoError| LoginsApiError::InvalidKey)?;
    Ok(encdec.check_canary(canary, text).unwrap_or(false))
}

#[handle_error(Error)]
pub fn create_key() -> ApiResult<String> {
    Ok(jwcrypto::EncryptorDecryptor::create_key()?)
}

#[cfg(test)]
pub mod test_utils {
    use super::*;
    use serde::{de::DeserializeOwned, Serialize};

    lazy_static::lazy_static! {
        pub static ref TEST_ENCRYPTION_KEY: String = serde_json::to_string(&jwcrypto::Jwk::new_direct_key(Some("test-key".to_string())).unwrap()).unwrap();
        pub static ref TEST_ENCDEC: Arc<ManagedEncryptorDecryptor> = Arc::new(ManagedEncryptorDecryptor::new(Arc::new(StaticKeyManager { key: TEST_ENCRYPTION_KEY.clone() })));
    }

    pub fn encrypt_struct<T: Serialize>(fields: &T) -> String {
        let string = serde_json::to_string(fields).unwrap();
        let cipherbytes = TEST_ENCDEC.encrypt(string.as_bytes().into()).unwrap();
        std::str::from_utf8(&cipherbytes).unwrap().to_owned()
    }
    pub fn decrypt_struct<T: DeserializeOwned>(ciphertext: String) -> T {
        let jsonbytes = TEST_ENCDEC.decrypt(ciphertext.as_bytes().into()).unwrap();
        serde_json::from_str(std::str::from_utf8(&jsonbytes).unwrap()).unwrap()
    }
}

#[cfg(not(feature = "keydb"))]
#[cfg(test)]
mod tests {
    use super::*;
    use nss_as::ensure_initialized;

    #[test]
    fn test_static_key_manager() {
        ensure_initialized();
        let key = create_key().unwrap();
        let key_manager = StaticKeyManager { key: key.clone() };
        assert_eq!(key.as_bytes(), key_manager.get_key().unwrap());
    }

    #[test]
    fn test_managed_encdec_with_invalid_key() {
        ensure_initialized();
        let key_manager = Arc::new(StaticKeyManager {
            key: "bad_key".to_owned(),
        });
        let encdec = ManagedEncryptorDecryptor { key_manager };
        assert!(matches!(
            encdec.encrypt("secret".as_bytes().into()).err().unwrap(),
            LoginsApiError::InvalidKey
        ));
    }

    #[test]
    fn test_managed_encdec_with_missing_key() {
        ensure_initialized();
        struct MyKeyManager {}
        impl KeyManager for MyKeyManager {
            fn get_key(&self) -> ApiResult<Vec<u8>> {
                Err(LoginsApiError::MissingKey)
            }
        }
        let key_manager = Arc::new(MyKeyManager {});
        let encdec = ManagedEncryptorDecryptor { key_manager };
        assert!(matches!(
            encdec.encrypt("secret".as_bytes().into()).err().unwrap(),
            LoginsApiError::MissingKey
        ));
    }

    #[test]
    fn test_managed_encdec() {
        ensure_initialized();
        let key = create_key().unwrap();
        let key_manager = Arc::new(StaticKeyManager { key });
        let encdec = ManagedEncryptorDecryptor { key_manager };
        let cleartext = "secret";
        let ciphertext = encdec.encrypt(cleartext.as_bytes().into()).unwrap();
        assert_eq!(
            encdec.decrypt(ciphertext.clone()).unwrap(),
            cleartext.as_bytes()
        );
        let other_encdec = ManagedEncryptorDecryptor {
            key_manager: Arc::new(StaticKeyManager {
                key: create_key().unwrap(),
            }),
        };

        assert_eq!(
            other_encdec.decrypt(ciphertext).err().unwrap().to_string(),
            "decryption failed: Crypto error: NSS error: NSS error: -8190 "
        );
    }

    #[test]
    fn test_key_error() {
        let storage_err = jwcrypto::EncryptorDecryptor::new("bad-key").err().unwrap();
        println!("{storage_err:?}");
        assert!(matches!(storage_err, jwcrypto::JwCryptoError::InvalidKey));
    }

    #[test]
    fn test_canary_functionality() {
        ensure_initialized();
        const CANARY_TEXT: &str = "Arbitrary sequence of text";
        let key = create_key().unwrap();
        let canary = create_canary(CANARY_TEXT, &key).unwrap();
        assert!(check_canary(&canary, CANARY_TEXT, &key).unwrap());

        let different_key = create_key().unwrap();
        assert!(!check_canary(&canary, CANARY_TEXT, &different_key).unwrap());

        let bad_key = "bad_key".to_owned();
        assert!(matches!(
            check_canary(&canary, CANARY_TEXT, &bad_key).err().unwrap(),
            LoginsApiError::InvalidKey
        ));
    }
}

#[cfg(feature = "keydb")]
#[cfg(test)]
mod tests_keydb {
    use super::*;
    use nss_as::ensure_initialized_with_profile_dir;
    use std::path::PathBuf;

    struct MockPrimaryPasswordAuthenticator {
        password: String,
    }

    #[async_trait]
    impl PrimaryPasswordAuthenticator for MockPrimaryPasswordAuthenticator {
        async fn get_primary_password(&self) -> ApiResult<String> {
            Ok(self.password.clone())
        }
        async fn on_authentication_success(&self) -> ApiResult<()> {
            Ok(())
        }
        async fn on_authentication_failure(&self) -> ApiResult<()> {
            Ok(())
        }
    }

    fn profile_path() -> PathBuf {
        std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../support/rc_crypto/nss/fixtures/profile")
    }

    #[test]
    fn test_ensure_initialized_with_profile_dir() {
        ensure_initialized_with_profile_dir(profile_path());
    }

    #[test]
    fn test_create_key() {
        ensure_initialized_with_profile_dir(profile_path());
        let key = create_key().unwrap();
        assert_eq!(key.len(), 63)
    }

    #[test]
    fn test_nss_key_manager() {
        ensure_initialized_with_profile_dir(profile_path());
        // `password` is the primary password of the profile fixture
        let mock_primary_password_authenticator = MockPrimaryPasswordAuthenticator {
            password: "password".to_string(),
        };
        let nss_key_manager = NSSKeyManager::new(Arc::new(mock_primary_password_authenticator));
        // key from fixtures/profile/key4.db
        let expected = [
            123, 34, 107, 116, 121, 34, 58, 34, 111, 99, 116, 34, 44, 34, 107, 34, 58, 34, 66, 74,
            104, 84, 108, 103, 51, 118, 56, 49, 65, 66, 51, 118, 87, 50, 71, 122, 54, 104, 69, 54,
            84, 116, 75, 83, 112, 85, 102, 84, 86, 75, 73, 83, 99, 74, 45, 77, 78, 83, 67, 117, 99,
            34, 125,
        ]
        .to_vec();
        assert_eq!(nss_key_manager.get_key().unwrap(), expected);
    }

    #[test]
    fn test_nss_key_manager_caching() {
        ensure_initialized_with_profile_dir(profile_path());
        // `password` is the primary password of the profile fixture
        let nss_key_manager = NSSKeyManager::new(Arc::new(MockPrimaryPasswordAuthenticator {
            password: "password".to_string(),
        }));

        let key = nss_key_manager.get_key().unwrap();
        assert_eq!(*nss_key_manager.cached_key.read(), Some(key.clone()));

        // A sentinel in the cache tells a cached key apart from a freshly fetched one.
        let sentinel = b"sentinel".to_vec();
        *nss_key_manager.cached_key.write() = Some(sentinel.clone());
        assert_eq!(nss_key_manager.get_key().unwrap(), sentinel);

        // Authenticating with a wrong password logs out of the token, so it is locked again and
        // the cache must be dropped.
        assert!(!authenticate_with_primary_password("wrong password").unwrap());
        assert_eq!(nss_key_manager.get_key().unwrap(), key);
        assert_eq!(*nss_key_manager.cached_key.read(), Some(key));
    }

    #[test]
    fn test_primary_password_authentication() {
        ensure_initialized_with_profile_dir(profile_path());
        assert!(authenticate_with_primary_password("password").unwrap());
    }
}
