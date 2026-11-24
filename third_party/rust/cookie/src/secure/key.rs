use std::convert::TryFrom;

const SIGNING_KEY_LEN: usize = 32;
const ENCRYPTION_KEY_LEN: usize = 32;
const COMBINED_KEY_LENGTH: usize = SIGNING_KEY_LEN + ENCRYPTION_KEY_LEN;

// Statically ensure the numbers above are in-sync.
#[cfg(feature = "signed")]
const_assert!(crate::secure::signed::KEY_LEN == SIGNING_KEY_LEN);
#[cfg(feature = "private")]
const_assert!(crate::secure::private::KEY_LEN == ENCRYPTION_KEY_LEN);

/// A cryptographic master key for use with `Signed` and/or `Private` jars.
///
/// This structure encapsulates secure, cryptographic keys for use with both
/// [`PrivateJar`](crate::PrivateJar) and [`SignedJar`](crate::SignedJar). A
/// single instance of a `Key` can be used for both a `PrivateJar` and a
/// `SignedJar` simultaneously with no notable security implications.
#[cfg_attr(all(nightly, doc), doc(cfg(any(feature = "private", feature = "signed"))))]
#[derive(Clone)]
pub struct Key([u8; COMBINED_KEY_LENGTH /* SIGNING | ENCRYPTION */]);

impl PartialEq for Key {
    fn eq(&self, other: &Self) -> bool {
        use subtle::ConstantTimeEq;

        self.0.ct_eq(&other.0).into()
    }
}

impl Key {
    // An empty key structure, to be filled.
    const fn zero() -> Self {
        Key([0; COMBINED_KEY_LENGTH])
    }

    /// Creates a new `Key` from a 512-bit cryptographically random string.
    ///
    /// The supplied key must be at least 512-bits (64 bytes). For security, the
    /// master key _must_ be cryptographically random.
    ///
    /// # Panics
    ///
    /// Panics if `key` is less than 64 bytes in length.
    ///
    /// For a non-panicking version, use [`Key::try_from()`] or generate a key with
    /// [`Key::generate()`] or [`Key::try_generate()`].
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Key;
    ///
    /// # /*
    /// let key = { /* a cryptographically random key >= 64 bytes */ };
    /// # */
    /// # let key: &Vec<u8> = &(0..64).collect();
    ///
    /// let key = Key::from(key);
    /// ```
    #[inline]
    pub fn from(key: &[u8]) -> Key {
        Key::try_from(key).unwrap()
    }

    /// Derives new signing/encryption keys from a master key.
    ///
    /// The master key must be at least 256-bits (32 bytes). For security, the
    /// master key _must_ be cryptographically random. The keys are derived
    /// deterministically from the master key.
    ///
    /// # Panics
    ///
    /// Panics if `key` is less than 32 bytes in length.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Key;
    ///
    /// # /*
    /// let master_key = { /* a cryptographically random key >= 32 bytes */ };
    /// # */
    /// # let master_key: &Vec<u8> = &(0..32).collect();
    ///
    /// let key = Key::derive_from(master_key);
    /// ```
    #[cfg(feature = "key-expansion")]
    #[cfg_attr(all(nightly, doc), doc(cfg(feature = "key-expansion")))]
    pub fn derive_from(master_key: &[u8]) -> Self {
        if master_key.len() < 32 {
            panic!("bad master key length: expected >= 32 bytes, found {}", master_key.len());
        }

        // Expand the master key into two HKDF generated keys.
        const KEYS_INFO: &[u8] = b"COOKIE;SIGNED:HMAC-SHA256;PRIVATE:AEAD-AES-256-GCM";
        let mut both_keys = [0; COMBINED_KEY_LENGTH];
        let hk = hkdf::Hkdf::<sha2::Sha256>::from_prk(master_key).expect("key length prechecked");
        hk.expand(KEYS_INFO, &mut both_keys).expect("expand into keys");
        Key::from(&both_keys)
    }

    /// Generates signing/encryption keys from a secure, random source. Keys are
    /// generated nondeterministically.
    ///
    /// # Panics
    ///
    /// Panics if randomness cannot be retrieved from the operating system. See
    /// [`Key::try_generate()`] for a non-panicking version.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Key;
    ///
    /// let key = Key::generate();
    /// ```
    pub fn generate() -> Key {
        Self::try_generate().expect("failed to generate `Key` from randomness")
    }

    /// Attempts to generate signing/encryption keys from a secure, random
    /// source. Keys are generated nondeterministically. If randomness cannot be
    /// retrieved from the underlying operating system, returns `None`.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Key;
    ///
    /// let key = Key::try_generate();
    /// ```
    pub fn try_generate() -> Option<Key> {
        use crate::secure::rand::RngCore;

        let mut rng = crate::secure::rand::thread_rng();
        let mut key = Key::zero();
        rng.try_fill_bytes(&mut key.0).ok()?;
        Some(key)
    }

    /// Returns the raw bytes of a key suitable for signing cookies. Guaranteed
    /// to be at least 32 bytes.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Key;
    ///
    /// let key = Key::generate();
    /// let signing_key = key.signing();
    /// ```
    pub fn signing(&self) -> &[u8] {
        &self.0[..SIGNING_KEY_LEN]
    }

    /// Returns the raw bytes of a key suitable for encrypting cookies.
    /// Guaranteed to be at least 32 bytes.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Key;
    ///
    /// let key = Key::generate();
    /// let encryption_key = key.encryption();
    /// ```
    pub fn encryption(&self) -> &[u8] {
        &self.0[SIGNING_KEY_LEN..]
    }

    /// Returns the raw bytes of the master key. Guaranteed to be at least 64
    /// bytes.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Key;
    ///
    /// let key = Key::generate();
    /// let master_key = key.master();
    /// ```
    pub fn master(&self) -> &[u8] {
        &self.0
    }
}

/// An error indicating an issue with generating or constructing a key.
#[cfg_attr(all(nightly, doc), doc(cfg(any(feature = "private", feature = "signed"))))]
#[derive(Debug)]
#[non_exhaustive]
pub enum KeyError {
    /// Too few bytes (`.0`) were provided to generate a key.
    ///
    /// See [`Key::from()`] for minimum requirements.
    TooShort(usize),
}

impl std::error::Error for KeyError { }

impl std::fmt::Display for KeyError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            KeyError::TooShort(n) => {
                write!(f, "key material is too short: expected >= {} bytes, got {} bytes",
                       COMBINED_KEY_LENGTH, n)
            }
        }
    }
}

impl TryFrom<&[u8]> for Key {
    type Error = KeyError;

    /// A fallible version of [`Key::from()`].
    ///
    /// Succeeds when [`Key::from()`] succeds and returns an error where
    /// [`Key::from()`] panics, namely, if `key` is too short.
    ///
    /// # Example
    ///
    /// ```rust
    /// # use std::convert::TryFrom;
    /// use cookie::Key;
    ///
    /// # /*
    /// let key = { /* a cryptographically random key >= 64 bytes */ };
    /// # */
    /// # let key: &Vec<u8> = &(0..64).collect();
    /// # let key: &[u8] = &key[..];
    /// assert!(Key::try_from(key).is_ok());
    ///
    /// // A key that's far too short to use.
    /// let key = &[1, 2, 3, 4][..];
    /// assert!(Key::try_from(key).is_err());
    /// ```
    fn try_from(key: &[u8]) -> Result<Self, Self::Error> {
        if key.len() < COMBINED_KEY_LENGTH {
            Err(KeyError::TooShort(key.len()))
        } else {
            let mut output = Key::zero();
            output.0.copy_from_slice(&key[..COMBINED_KEY_LENGTH]);
            Ok(output)
        }
    }
}

#[cfg(test)]
mod test {
    use super::Key;

    #[test]
    fn from_works() {
        let key = Key::from(&(0..64).collect::<Vec<_>>());

        let signing: Vec<u8> = (0..32).collect();
        assert_eq!(key.signing(), &*signing);

        let encryption: Vec<u8> = (32..64).collect();
        assert_eq!(key.encryption(), &*encryption);
    }

    #[test]
    fn try_from_works() {
        use core::convert::TryInto;
        let data = (0..64).collect::<Vec<_>>();
        let key_res: Result<Key, _> = data[0..63].try_into();
        assert!(key_res.is_err());

        let key_res: Result<Key, _> = data.as_slice().try_into();
        assert!(key_res.is_ok());
    }

    #[test]
    #[cfg(feature = "key-expansion")]
    fn deterministic_derive() {
        let master_key: Vec<u8> = (0..32).collect();

        let key_a = Key::derive_from(&master_key);
        let key_b = Key::derive_from(&master_key);

        assert_eq!(key_a.signing(), key_b.signing());
        assert_eq!(key_a.encryption(), key_b.encryption());
        assert_ne!(key_a.encryption(), key_a.signing());

        let master_key_2: Vec<u8> = (32..64).collect();
        let key_2 = Key::derive_from(&master_key_2);

        assert_ne!(key_2.signing(), key_a.signing());
        assert_ne!(key_2.encryption(), key_a.encryption());
    }

    #[test]
    fn non_deterministic_generate() {
        let key_a = Key::generate();
        let key_b = Key::generate();

        assert_ne!(key_a.signing(), key_b.signing());
        assert_ne!(key_a.encryption(), key_b.encryption());
    }
}
