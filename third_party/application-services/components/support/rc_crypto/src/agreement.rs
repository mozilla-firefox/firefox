/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file contains code that was copied from the ring crate which is under
// the ISC license, reproduced below:

// Copyright 2015-2017 Brian Smith.

// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
// OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

use crate::error::*;
use core::marker::PhantomData;

pub use ec::{Curve, EcKey};
use nss_as::{ec, ecdh};

pub type EphemeralKeyPair = KeyPair<Ephemeral>;

/// A key agreement algorithm.
#[derive(PartialEq, Eq)]
pub struct Algorithm {
    pub(crate) curve_id: ec::Curve,
}

pub static ECDH_P256: Algorithm = Algorithm {
    curve_id: ec::Curve::P256,
};

pub static ECDH_P384: Algorithm = Algorithm {
    curve_id: ec::Curve::P384,
};

/// How many times the key may be used.
pub trait Lifetime {}

/// The key may be used at most once.
pub struct Ephemeral {}
impl Lifetime for Ephemeral {}

/// The key may be used more than once.
pub struct Static {}
impl Lifetime for Static {}

/// A key pair for key agreement.
pub struct KeyPair<U: Lifetime> {
    private_key: PrivateKey<U>,
    public_key: PublicKey,
}

impl<U: Lifetime> KeyPair<U> {
    /// Generate a new key pair for the given algorithm.
    pub fn generate(alg: &'static Algorithm) -> Result<Self> {
        let (prv_key, pub_key) = ec::generate_keypair(alg.curve_id)?;
        Ok(Self {
            private_key: PrivateKey {
                alg,
                wrapped: prv_key,
                usage: PhantomData,
            },
            public_key: PublicKey {
                alg,
                wrapped: pub_key,
            },
        })
    }

    pub fn from_private_key(private_key: PrivateKey<U>) -> Result<Self> {
        let public_key = private_key
            .compute_public_key()
            .map_err(|_| ErrorKind::InternalError)?;
        Ok(Self {
            private_key,
            public_key,
        })
    }

    /// The private key.
    pub fn private_key(&self) -> &PrivateKey<U> {
        &self.private_key
    }

    /// The public key.
    pub fn public_key(&self) -> &PublicKey {
        &self.public_key
    }

    /// Split the key pair apart.
    pub fn split(self) -> (PrivateKey<U>, PublicKey) {
        (self.private_key, self.public_key)
    }
}

impl KeyPair<Static> {
    pub fn from(private_key: PrivateKey<Static>) -> Result<Self> {
        Self::from_private_key(private_key)
    }
}

/// A public key for key agreement.
pub struct PublicKey {
    wrapped: ec::PublicKey,
    alg: &'static Algorithm,
}

impl PublicKey {
    #[inline]
    pub fn to_bytes(&self) -> Result<Vec<u8>> {
        Ok(self.wrapped.to_bytes()?)
    }

    #[inline]
    pub fn algorithm(&self) -> &'static Algorithm {
        self.alg
    }
}

/// An unparsed public key for key agreement.
pub struct UnparsedPublicKey<'a> {
    alg: &'static Algorithm,
    bytes: &'a [u8],
}

impl<'a> UnparsedPublicKey<'a> {
    pub fn new(algorithm: &'static Algorithm, bytes: &'a [u8]) -> Self {
        Self {
            alg: algorithm,
            bytes,
        }
    }

    pub fn algorithm(&self) -> &'static Algorithm {
        self.alg
    }

    pub fn bytes(&self) -> &'a [u8] {
        self.bytes
    }
}

/// A private key for key agreement.
pub struct PrivateKey<U: Lifetime> {
    wrapped: ec::PrivateKey,
    alg: &'static Algorithm,
    usage: PhantomData<U>,
}

impl<U: Lifetime> PrivateKey<U> {
    #[inline]
    pub fn algorithm(&self) -> &'static Algorithm {
        self.alg
    }

    pub fn compute_public_key(&self) -> Result<PublicKey> {
        let pub_key = self.wrapped.convert_to_public_key()?;
        Ok(PublicKey {
            wrapped: pub_key,
            alg: self.alg,
        })
    }

    /// Ephemeral agreement.
    /// This consumes `self`, ensuring that the private key can
    /// only be used for a single agreement operation.
    pub fn agree(self, peer_public_key: &UnparsedPublicKey<'_>) -> Result<InputKeyMaterial> {
        agree_(&self.wrapped, self.alg, peer_public_key)
    }
}

impl PrivateKey<Static> {
    /// Static agreement.
    /// This borrows `self`, allowing the private key to
    /// be used for a multiple agreement operations.
    pub fn agree_static(
        &self,
        peer_public_key: &UnparsedPublicKey<'_>,
    ) -> Result<InputKeyMaterial> {
        agree_(&self.wrapped, self.alg, peer_public_key)
    }

    pub fn import(ec_key: &EcKey) -> Result<Self> {
        // XXX: we should just let ec::PrivateKey own alg.
        let alg = match ec_key.curve() {
            Curve::P256 => &ECDH_P256,
            Curve::P384 => &ECDH_P384,
        };
        let private_key = ec::PrivateKey::import(ec_key)?;
        Ok(Self {
            wrapped: private_key,
            alg,
            usage: PhantomData,
        })
    }

    pub fn export(&self) -> Result<EcKey> {
        Ok(self.wrapped.export()?)
    }

    /// The whole point of having `Ephemeral` and `Static` lifetimes is to use the type
    /// system to avoid re-using the same ephemeral key. However for tests we might need
    /// to create a "static" ephemeral key.
    pub fn _tests_only_dangerously_convert_to_ephemeral(self) -> PrivateKey<Ephemeral> {
        PrivateKey::<Ephemeral> {
            wrapped: self.wrapped,
            alg: self.alg,
            usage: PhantomData,
        }
    }
}

fn agree_(
    my_private_key: &ec::PrivateKey,
    my_alg: &Algorithm,
    peer_public_key: &UnparsedPublicKey<'_>,
) -> Result<InputKeyMaterial> {
    let alg = &my_alg;
    if peer_public_key.algorithm() != *alg {
        return Err(ErrorKind::InternalError.into());
    }
    let pub_key = ec::PublicKey::from_bytes(my_private_key.curve(), peer_public_key.bytes())?;
    let value = ecdh::ecdh_agreement(my_private_key, &pub_key)?;
    Ok(InputKeyMaterial { value })
}

/// The result of a key agreement operation, to be fed into a KDF.
#[must_use]
pub struct InputKeyMaterial {
    value: Vec<u8>,
}

impl InputKeyMaterial {
    /// Calls `kdf` with the raw key material and then returns what `kdf`
    /// returns, consuming `Self` so that the key material can only be used
    /// once.
    pub fn derive<F, R>(self, kdf: F) -> R
    where
        F: FnOnce(&[u8]) -> R,
    {
        kdf(&self.value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
    use nss_as::ensure_initialized;

    // Test vectors copied from:
    // https://chromium.googlesource.com/chromium/src/+/56f1232/components/test/data/webcrypto/ecdh.json#5

    const PUB_KEY_1_B64: &str =
        "BLunVoWkR67xRdAohVblFBWn1Oosb3kH_baxw1yfIYFfthSm4LIY35vDD-5LE454eB7TShn919DVVGZ_7tWdjTE";
    const PRIV_KEY_1_JWK_D: &str = "CQ8uF_-zB1NftLO6ytwKM3Cnuol64PQw5qOuCzQJeFU";
    const PRIV_KEY_1_JWK_X: &str = "u6dWhaRHrvFF0CiFVuUUFafU6ixveQf9trHDXJ8hgV8";
    const PRIV_KEY_1_JWK_Y: &str = "thSm4LIY35vDD-5LE454eB7TShn919DVVGZ_7tWdjTE";

    const PRIV_KEY_2_JWK_D: &str = "uN2YSQvxuxhQQ9Y1XXjYi1vr2ZTdzuoDX18PYu4LU-0";
    const PRIV_KEY_2_JWK_X: &str = "S2S3tjygMB0DkM-N9jYUgGLt_9_H6km5P9V6V_KS4_4";
    const PRIV_KEY_2_JWK_Y: &str = "03j8Tyqgrc4R4FAUV2C7-im96yMmfmO_5Om6Kr8YP3o";

    const SHARED_SECRET_HEX: &str =
        "163FAA3FC4815D47345C8E959F707B2F1D3537E7B2EA1DAEC23CA8D0A242CFF3";

    fn load_priv_key_1() -> PrivateKey<Static> {
        let private_key = URL_SAFE_NO_PAD.decode(PRIV_KEY_1_JWK_D).unwrap();
        let x = URL_SAFE_NO_PAD.decode(PRIV_KEY_1_JWK_X).unwrap();
        let y = URL_SAFE_NO_PAD.decode(PRIV_KEY_1_JWK_Y).unwrap();
        PrivateKey::<Static>::import(
            &EcKey::from_coordinates(Curve::P256, &private_key, &x, &y).unwrap(),
        )
        .unwrap()
    }

    fn load_priv_key_2() -> PrivateKey<Static> {
        let private_key = URL_SAFE_NO_PAD.decode(PRIV_KEY_2_JWK_D).unwrap();
        let x = URL_SAFE_NO_PAD.decode(PRIV_KEY_2_JWK_X).unwrap();
        let y = URL_SAFE_NO_PAD.decode(PRIV_KEY_2_JWK_Y).unwrap();
        PrivateKey::<Static>::import(
            &EcKey::from_coordinates(Curve::P256, &private_key, &x, &y).unwrap(),
        )
        .unwrap()
    }

    #[test]
    fn test_static_agreement() {
        ensure_initialized();
        let pub_key_raw = URL_SAFE_NO_PAD.decode(PUB_KEY_1_B64).unwrap();
        let peer_pub_key = UnparsedPublicKey::new(&ECDH_P256, &pub_key_raw);
        let prv_key = load_priv_key_2();
        let ikm = prv_key.agree_static(&peer_pub_key).unwrap();
        let secret = ikm
            .derive(|z| -> Result<Vec<u8>> { Ok(z.to_vec()) })
            .unwrap();
        let secret_b64 = hex::encode_upper(secret);
        assert_eq!(secret_b64, *SHARED_SECRET_HEX);
    }

    #[test]
    fn test_ephemeral_agreement_roundtrip() {
        ensure_initialized();
        let (our_prv_key, our_pub_key) =
            KeyPair::<Ephemeral>::generate(&ECDH_P256).unwrap().split();
        let (their_prv_key, their_pub_key) =
            KeyPair::<Ephemeral>::generate(&ECDH_P256).unwrap().split();
        let their_pub_key_raw = their_pub_key.to_bytes().unwrap();
        let peer_public_key_1 = UnparsedPublicKey::new(&ECDH_P256, &their_pub_key_raw);
        let ikm_1 = our_prv_key.agree(&peer_public_key_1).unwrap();
        let secret_1 = ikm_1
            .derive(|z| -> Result<Vec<u8>> { Ok(z.to_vec()) })
            .unwrap();
        let our_pub_key_raw = our_pub_key.to_bytes().unwrap();
        let peer_public_key_2 = UnparsedPublicKey::new(&ECDH_P256, &our_pub_key_raw);
        let ikm_2 = their_prv_key.agree(&peer_public_key_2).unwrap();
        let secret_2 = ikm_2
            .derive(|z| -> Result<Vec<u8>> { Ok(z.to_vec()) })
            .unwrap();
        assert_eq!(secret_1, secret_2);
    }

    #[test]
    fn test_compute_public_key() {
        ensure_initialized();
        let (prv_key, pub_key) = KeyPair::<Static>::generate(&ECDH_P256).unwrap().split();
        let computed_pub_key = prv_key.compute_public_key().unwrap();
        assert_eq!(
            computed_pub_key.to_bytes().unwrap(),
            pub_key.to_bytes().unwrap()
        );
    }

    #[test]
    fn test_compute_public_key_known_values() {
        ensure_initialized();
        let prv_key = load_priv_key_1();
        let pub_key = URL_SAFE_NO_PAD.decode(PUB_KEY_1_B64).unwrap();
        let computed_pub_key = prv_key.compute_public_key().unwrap();
        assert_eq!(computed_pub_key.to_bytes().unwrap(), pub_key.as_slice());

        let prv_key = load_priv_key_2();
        let computed_pub_key = prv_key.compute_public_key().unwrap();
        assert_ne!(computed_pub_key.to_bytes().unwrap(), pub_key.as_slice());
    }

    #[test]
    fn test_keys_byte_representations_roundtrip() {
        ensure_initialized();
        let key_pair = KeyPair::<Static>::generate(&ECDH_P256).unwrap();
        let prv_key = key_pair.private_key;
        let extracted_pub_key = prv_key.compute_public_key().unwrap();
        let ec_key = prv_key.export().unwrap();
        let prv_key_reconstructed = PrivateKey::<Static>::import(&ec_key).unwrap();
        let extracted_pub_key_reconstructed = prv_key.compute_public_key().unwrap();
        let ec_key_reconstructed = prv_key_reconstructed.export().unwrap();
        assert_eq!(ec_key.curve(), ec_key_reconstructed.curve());
        assert_eq!(ec_key.public_key(), ec_key_reconstructed.public_key());
        assert_eq!(ec_key.private_key(), ec_key_reconstructed.private_key());
        assert_eq!(
            extracted_pub_key.to_bytes().unwrap(),
            extracted_pub_key_reconstructed.to_bytes().unwrap()
        );
    }

    #[test]
    fn test_agreement_rejects_invalid_pubkeys() {
        ensure_initialized();
        let prv_key = load_priv_key_2();

        let mut invalid_pub_key = URL_SAFE_NO_PAD.decode(PUB_KEY_1_B64).unwrap();
        invalid_pub_key[0] = invalid_pub_key[0].wrapping_add(1);
        assert!(prv_key
            .agree_static(&UnparsedPublicKey::new(&ECDH_P256, &invalid_pub_key))
            .is_err());

        let mut invalid_pub_key = URL_SAFE_NO_PAD.decode(PUB_KEY_1_B64).unwrap();
        invalid_pub_key[0] = 0x02;
        assert!(prv_key
            .agree_static(&UnparsedPublicKey::new(&ECDH_P256, &invalid_pub_key))
            .is_err());

        let mut invalid_pub_key = URL_SAFE_NO_PAD.decode(PUB_KEY_1_B64).unwrap();
        invalid_pub_key[64] = invalid_pub_key[0].wrapping_add(1);
        assert!(prv_key
            .agree_static(&UnparsedPublicKey::new(&ECDH_P256, &invalid_pub_key))
            .is_err());

        let invalid_pub_key = [0u8; 65];
        assert!(prv_key
            .agree_static(&UnparsedPublicKey::new(&ECDH_P256, &invalid_pub_key))
            .is_err());

        let mut invalid_pub_key = URL_SAFE_NO_PAD.decode(PUB_KEY_1_B64).unwrap().to_vec();
        invalid_pub_key = invalid_pub_key[0..64].to_vec();
        assert!(prv_key
            .agree_static(&UnparsedPublicKey::new(&ECDH_P256, &invalid_pub_key))
            .is_err());

        // From FxA tests at https://github.com/mozilla/fxa-crypto-relier/blob/04f61dc/test/deriver/DeriverUtils.js#L78
        // We trust that NSS will do the right thing here, but it seems worthwhile to confirm for completeness.
        let invalid_pub_key_b64 = "BEogZ-rnm44oJkKsOE6Tc7NwFMgmntf7Btm_Rc4atxcqq99Xq1RWNTFpk99pdQOSjUvwELss51PkmAGCXhLfMV0";
        let invalid_pub_key = URL_SAFE_NO_PAD.decode(invalid_pub_key_b64).unwrap();
        assert!(prv_key
            .agree_static(&UnparsedPublicKey::new(&ECDH_P256, &invalid_pub_key))
            .is_err());
    }
}
