/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::ctap2::commands::client_pin::PinUvAuthTokenPermission;
use crate::ctap2::commands::get_info::{AuthenticatorInfo, AuthenticatorVersion};
use crate::errors::AuthenticatorError;
use crate::{ctap2::commands::CommandError, transport::errors::HIDError};
use serde::{
    de::{Error as SerdeError, MapAccess, Unexpected, Visitor},
    Deserialize, Deserializer, Serialize, Serializer,
};
use serde_bytes::ByteBuf;
use std::convert::TryFrom;
use std::fmt;

mod nss;
use nss as backend;

use backend::{
    decrypt_aes_256_cbc_no_pad, ecdhe_p256_raw, encrypt_aes_256_cbc_no_pad, gen_p256, hmac_sha256,
    random_bytes, sha256,
};

mod der;

pub use backend::ecdsa_p256_sha256_sign_raw;

pub struct PinUvAuthProtocol(pub(crate) Box<dyn PinProtocolImpl + Send + Sync>);
impl PinUvAuthProtocol {
    pub fn from_id(id: u64) -> Option<Self> {
        match id {
            1 => Some(Self(Box::new(PinUvAuth1 {}))),
            2 => Some(Self(Box::new(PinUvAuth2 {}))),
            _ => None,
        }
    }

    pub fn id(&self) -> u64 {
        self.0.protocol_id()
    }

    pub fn encapsulate(&self, peer_cose_key: &COSEKey) -> Result<SharedSecret, CryptoError> {
        self.0.encapsulate(peer_cose_key)
    }
}

/// The output of `PinUvAuthProtocol::encapsulate` is supposed to be used with the same
/// PinProtocolImpl. So we stash a copy of the calling PinUvAuthProtocol in the output SharedSecret.
/// We need a trick here to tell the compiler that every PinProtocolImpl we define will implement
/// Clone.
pub(crate) trait ClonablePinProtocolImpl {
    fn clone_box(&self) -> Box<dyn PinProtocolImpl + Send + Sync>;
}

impl<T> ClonablePinProtocolImpl for T
where
    T: 'static + PinProtocolImpl + Clone + Send + Sync,
{
    fn clone_box(&self) -> Box<dyn PinProtocolImpl + Send + Sync> {
        Box::new(self.clone())
    }
}

impl Clone for PinUvAuthProtocol {
    fn clone(&self) -> Self {
        PinUvAuthProtocol(self.0.as_ref().clone_box())
    }
}

/// CTAP 2.1, Section 6.5.4. PIN/UV Auth Protocol Abstract Definition
pub(crate) trait PinProtocolImpl: ClonablePinProtocolImpl {
    fn protocol_id(&self) -> u64;
    fn encrypt(&self, key: &[u8], plaintext: &[u8]) -> Result<Vec<u8>, CryptoError>;
    fn decrypt(&self, key: &[u8], ciphertext: &[u8]) -> Result<Vec<u8>, CryptoError>;
    fn authenticate(&self, key: &[u8], message: &[u8]) -> Result<Vec<u8>, CryptoError>;
    fn kdf(&self, z: &[u8]) -> Result<Vec<u8>, CryptoError>;
    fn encapsulate(&self, peer_cose_key: &COSEKey) -> Result<SharedSecret, CryptoError> {
        // [CTAP 2.1]
        // encapsulate(peerCoseKey) → (coseKey, sharedSecret) | error
        //      1) Let sharedSecret be the result of calling ecdh(peerCoseKey). Return any
        //         resulting error.
        //      2) Return (getPublicKey(), sharedSecret)
        //
        // ecdh(peerCoseKey) → sharedSecret | error
        //         Parse peerCoseKey as specified for getPublicKey, below, and produce a P-256
        //         point, Y. If unsuccessful, or if the resulting point is not on the curve, return
        //         error.  Calculate xY, the shared point. (I.e. the scalar-multiplication of the
        //         peer's point, Y, with the local private key agreement key.) Let Z be the
        //         32-byte, big-endian encoding of the x-coordinate of the shared point.  Return
        //         kdf(Z).

        match peer_cose_key.alg {
            // There is no COSEAlgorithm for ECDHE with the KDF used here. Section 6.5.6. of CTAP
            // 2.1 says to use value -25 (= ECDH_ES_HKDF256) even though "this is not the algorithm
            // actually used".
            COSEAlgorithm::ECDH_ES_HKDF256 => (),
            other => return Err(CryptoError::UnsupportedAlgorithm(other)),
        }

        let peer_cose_ec2_key = match peer_cose_key.key {
            COSEKeyType::EC2(ref key) => key,
            _ => return Err(CryptoError::UnsupportedKeyType),
        };

        let (shared_point, client_public_sec1) = ecdhe_p256_raw(peer_cose_ec2_key)?;

        let client_cose_ec2_key =
            COSEEC2Key::from_sec1_uncompressed(Curve::SECP256R1, &client_public_sec1)?;

        let client_cose_key = COSEKey {
            alg: COSEAlgorithm::ECDH_ES_HKDF256,
            key: COSEKeyType::EC2(client_cose_ec2_key),
        };

        let shared_secret = SharedSecret {
            pin_protocol: PinUvAuthProtocol(self.clone_box()),
            key: self.kdf(&shared_point)?,
            inputs: PublicInputs {
                peer: peer_cose_key.clone(),
                client: client_cose_key,
            },
        };

        Ok(shared_secret)
    }
}

impl TryFrom<&AuthenticatorInfo> for PinUvAuthProtocol {
    type Error = CommandError;

    fn try_from(info: &AuthenticatorInfo) -> Result<Self, Self::Error> {
        // CTAP 2.1, Section 6.5.5.4
        // "If there are multiple mutually supported protocols, and the platform
        // has no preference, it SHOULD select the one listed first in
        // pinUvAuthProtocols."
        if let Some(pin_protocols) = &info.pin_protocols {
            pin_protocols
                .iter()
                .copied()
                .find_map(PinUvAuthProtocol::from_id)
                .ok_or(CommandError::UnsupportedPinProtocol)
        } else {
            match info.max_supported_version() {
                AuthenticatorVersion::U2F_V2 | AuthenticatorVersion::Unknown => {
                    Err(CommandError::UnsupportedPinProtocol)
                }
                AuthenticatorVersion::FIDO_2_0 => Ok(PinUvAuthProtocol(Box::new(PinUvAuth1 {}))),
                AuthenticatorVersion::FIDO_2_1_PRE | AuthenticatorVersion::FIDO_2_1 => {
                    Ok(PinUvAuthProtocol(Box::new(PinUvAuth2 {})))
                }
            }
        }
    }
}

impl fmt::Debug for PinUvAuthProtocol {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("PinUvAuthProtocol")
            .field("id", &self.id())
            .finish()
    }
}

/// CTAP 2.1, Section 6.5.6.
#[derive(Copy, Clone)]
pub struct PinUvAuth1;

impl PinProtocolImpl for PinUvAuth1 {
    fn protocol_id(&self) -> u64 {
        1
    }

    fn encrypt(&self, key: &[u8], plaintext: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // [CTAP 2.1]
        // encrypt(key, demPlaintext) → ciphertext
        //      Return the AES-256-CBC encryption of plaintext using an all-zero IV. (No padding is
        //      performed as the size of plaintext is required to be a multiple of the AES block
        //      length.)
        encrypt_aes_256_cbc_no_pad(key, None, plaintext)
    }

    fn decrypt(&self, key: &[u8], ciphertext: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // [CTAP 2.1]
        // decrypt(key, demCiphertext) → plaintext | error
        //      If the size of ciphertext is not a multiple of the AES block length, return error.
        //      Otherwise return the AES-256-CBC decryption of ciphertext using an all-zero IV.
        decrypt_aes_256_cbc_no_pad(key, None, ciphertext)
    }

    fn authenticate(&self, key: &[u8], message: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // [CTAP 2.1]
        // authenticate(key, message) → signature
        //      Return the first 16 bytes of the result of computing HMAC-SHA-256 with the given
        //      key and message.
        let mut hmac = hmac_sha256(key, message)?;
        hmac.truncate(16);
        Ok(hmac)
    }

    fn kdf(&self, z: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // kdf(Z) → sharedSecret
        //         Return SHA-256(Z)
        sha256(z)
    }
}

/// CTAP 2.1, Section 6.5.7.
#[derive(Copy, Clone)]
pub struct PinUvAuth2;

impl PinProtocolImpl for PinUvAuth2 {
    fn protocol_id(&self) -> u64 {
        2
    }

    fn encrypt(&self, key: &[u8], plaintext: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // [CTAP 2.1]
        // encrypt(key, demPlaintext) → ciphertext
        //      1. Discard the first 32 bytes of key. (This selects the AES-key portion of the
        //         shared secret.)
        //      2. Let iv be a 16-byte, random bytestring.
        //      3. Let ct be the AES-256-CBC encryption of demPlaintext using key and iv. (No
        //         padding is performed as the size of demPlaintext is required to be a multiple of
        //         the AES block length.)
        //      4. Return iv || ct.
        if key.len() != 64 {
            return Err(CryptoError::LibraryFailure);
        }
        let key = &key[32..64];

        let iv = random_bytes(16)?;
        let mut ct = encrypt_aes_256_cbc_no_pad(key, Some(&iv), plaintext)?;

        let mut out = iv;
        out.append(&mut ct);
        Ok(out)
    }

    fn decrypt(&self, key: &[u8], ciphertext: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // decrypt(key, demCiphertext) → plaintext | error
        //      1. Discard the first 32 bytes of key. (This selects the AES-key portion of the
        //         shared secret.)
        //      2. If demCiphertext is less than 16 bytes in length, return an error
        //      3. Split demCiphertext after the 16th byte to produce two subspans, iv and ct.
        //      4. Return the AES-256-CBC decryption of ct using key and iv.
        if key.len() < 64 || ciphertext.len() < 16 {
            return Err(CryptoError::LibraryFailure);
        }
        let key = &key[32..64];
        let (iv, ct) = ciphertext.split_at(16);
        decrypt_aes_256_cbc_no_pad(key, Some(iv), ct)
    }

    fn authenticate(&self, key: &[u8], message: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // authenticate(key, message) → signature
        //      1. If key is longer than 32 bytes, discard the excess. (This selects the HMAC-key
        //         portion of the shared secret. When key is the pinUvAuthToken, it is exactly 32
        //         bytes long and thus this step has no effect.)
        //      2. Return the result of computing HMAC-SHA-256 on key and message.
        if key.len() < 32 {
            return Err(CryptoError::LibraryFailure);
        }
        let key = &key[0..32];
        hmac_sha256(key, message)
    }

    fn kdf(&self, z: &[u8]) -> Result<Vec<u8>, CryptoError> {
        // kdf(Z) → sharedSecret
        //      return HKDF-SHA-256(salt, Z, L = 32, info = "CTAP2 HMAC key") ||
        //             HKDF-SHA-256(salt, Z, L = 32, info = "CTAP2 AES key")
        // where salt = [0u8; 32].
        //
        // From Section 2 of RFC 5869, we have
        //   HKDF(salt, Z, 32, info) =
        //      HKDF-Expand(HKDF-Extract(salt, Z), info || 0x01)
        //
        // And for HKDF-SHA256 both Extract and Expand are instantiated with HMAC-SHA256.

        let prk = hmac_sha256(&[0u8; 32], z)?;
        let mut shared_secret = hmac_sha256(&prk, "CTAP2 HMAC key\x01".as_bytes())?;
        shared_secret.append(&mut hmac_sha256(&prk, "CTAP2 AES key\x01".as_bytes())?);
        Ok(shared_secret)
    }
}

#[derive(Clone, Debug)]
struct PublicInputs {
    client: COSEKey,
    peer: COSEKey,
}

#[derive(Clone, Debug)]
pub struct SharedSecret {
    pub pin_protocol: PinUvAuthProtocol,
    key: Vec<u8>,
    inputs: PublicInputs,
}

impl SharedSecret {
    pub fn encrypt(&self, plaintext: &[u8]) -> Result<Vec<u8>, CryptoError> {
        self.pin_protocol.0.encrypt(&self.key, plaintext)
    }
    pub fn decrypt(&self, ciphertext: &[u8]) -> Result<Vec<u8>, CryptoError> {
        self.pin_protocol.0.decrypt(&self.key, ciphertext)
    }
    pub fn decrypt_pin_token(
        &self,
        permissions: PinUvAuthTokenPermission,
        encrypted_pin_token: &[u8],
    ) -> Result<PinUvAuthToken, CryptoError> {
        let pin_token = self.decrypt(encrypted_pin_token)?;
        Ok(PinUvAuthToken {
            pin_protocol: self.pin_protocol.clone(),
            pin_token,
            permissions,
        })
    }
    pub fn authenticate(&self, message: &[u8]) -> Result<Vec<u8>, CryptoError> {
        self.pin_protocol.0.authenticate(&self.key, message)
    }
    pub fn client_input(&self) -> &COSEKey {
        &self.inputs.client
    }
    pub fn peer_input(&self) -> &COSEKey {
        &self.inputs.peer
    }

    #[cfg(test)]
    pub fn new_test(
        pin_protocol: PinUvAuthProtocol,
        key: Vec<u8>,
        client_input: COSEKey,
        peer_input: COSEKey,
    ) -> Self {
        Self {
            pin_protocol,
            key,
            inputs: PublicInputs {
                client: client_input,
                peer: peer_input,
            },
        }
    }
}

#[derive(Clone, Debug)]
pub struct PinUvAuthToken {
    pub pin_protocol: PinUvAuthProtocol,
    pin_token: Vec<u8>,
    pub permissions: PinUvAuthTokenPermission,
}

impl PinUvAuthToken {
    pub fn derive(self, message: &[u8]) -> Result<PinUvAuthParam, CryptoError> {
        let pin_auth = self.pin_protocol.0.authenticate(&self.pin_token, message)?;
        Ok(PinUvAuthParam {
            pin_auth,
            pin_protocol: self.pin_protocol,
            permissions: self.permissions,
        })
    }

    #[cfg(test)]
    pub fn new_test(
        pin_protocol: PinUvAuthProtocol,
        pin_token: Vec<u8>,
        permissions: PinUvAuthTokenPermission,
    ) -> Self {
        Self {
            pin_protocol,
            pin_token,
            permissions,
        }
    }
}

#[derive(Clone, Debug)]
pub struct PinUvAuthParam {
    pub(crate) pin_auth: Vec<u8>,
    pub pin_protocol: PinUvAuthProtocol,
    #[allow(dead_code)] // Not yet used
    pub(crate) permissions: PinUvAuthTokenPermission,
}

impl PinUvAuthParam {
    pub(crate) fn create_empty() -> Self {
        let pin_protocol = PinUvAuthProtocol(Box::new(PinUvAuth1 {}));
        Self {
            pin_auth: vec![],
            pin_protocol,
            permissions: PinUvAuthTokenPermission::empty(),
        }
    }

    #[cfg(test)]
    pub(crate) fn create_test(
        pin_protocol: u64,
        pin_auth: Vec<u8>,
        permissions: PinUvAuthTokenPermission,
    ) -> Self {
        let pin_protocol = PinUvAuthProtocol::try_from(&AuthenticatorInfo {
            pin_protocols: Some(vec![pin_protocol]),
            ..Default::default()
        })
        .expect("Failed to create PIN protocol");
        Self {
            pin_auth,
            pin_protocol,
            permissions,
        }
    }
}

impl Serialize for PinUvAuthParam {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serde_bytes::serialize(&self.pin_auth[..], serializer)
    }
}

/// A Curve identifier. You probably will never need to alter
/// or use this value, as it is set inside the Credential for you.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Curve {
    // +---------+-------+----------+------------------------------------+
    // | Name    | Value | Key Type | Description                        |
    // +---------+-------+----------+------------------------------------+
    // | P-256   | 1     | EC2      | NIST P-256 also known as secp256r1 |
    // | P-384   | 2     | EC2      | NIST P-384 also known as secp384r1 |
    // | P-521   | 3     | EC2      | NIST P-521 also known as secp521r1 |
    // | X25519  | 4     | OKP      | X25519 for use w/ ECDH only        |
    // | X448    | 5     | OKP      | X448 for use w/ ECDH only          |
    // | Ed25519 | 6     | OKP      | Ed25519 for use w/ EdDSA only      |
    // | Ed448   | 7     | OKP      | Ed448 for use w/ EdDSA only        |
    // +---------+-------+----------+------------------------------------+
    /// Identifies this curve as SECP256R1 (X9_62_PRIME256V1 in OpenSSL)
    SECP256R1 = 1,
    /// Identifies this curve as SECP384R1
    SECP384R1 = 2,
    /// Identifies this curve as SECP521R1
    SECP521R1 = 3,
    /// Identifieds this as OKP X25519 for use w/ ECDH only
    X25519 = 4,
    /// Identifieds this as OKP X448 for use w/ ECDH only
    X448 = 5,
    /// Identifieds this as OKP Ed25519 for use w/ EdDSA only
    Ed25519 = 6,
    /// Identifieds this as OKP Ed448 for use w/ EdDSA only
    Ed448 = 7,
}

impl Serialize for Curve {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_i64(*self as i64)
    }
}

impl TryFrom<i64> for Curve {
    type Error = CryptoError;
    fn try_from(i: i64) -> Result<Self, Self::Error> {
        match i {
            i if i == Curve::SECP256R1 as i64 => Ok(Curve::SECP256R1),
            i if i == Curve::SECP384R1 as i64 => Ok(Curve::SECP384R1),
            i if i == Curve::SECP521R1 as i64 => Ok(Curve::SECP521R1),
            i if i == Curve::X25519 as i64 => Ok(Curve::X25519),
            i if i == Curve::X448 as i64 => Ok(Curve::X448),
            i if i == Curve::Ed25519 as i64 => Ok(Curve::Ed25519),
            i if i == Curve::Ed448 as i64 => Ok(Curve::Ed448),
            _ => Err(CryptoError::UnknownKeyType),
        }
    }
}
/// A COSE signature algorithm, indicating the type of key and hash type
/// that should be used.
/// see: https://www.iana.org/assignments/cose/cose.xhtml#table-algorithms
#[rustfmt::skip]
#[allow(non_camel_case_types)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum COSEAlgorithm {
    // /// Identifies this key as ECDSA (recommended SECP256R1) with SHA256 hashing
    // //#[serde(alias = "ECDSA_SHA256")]
    // ES256 = -7, // recommends curve SECP256R1
    // /// Identifies this key as ECDSA (recommended SECP384R1) with SHA384 hashing
    // //#[serde(alias = "ECDSA_SHA384")]
    // ES384 = -35, // recommends curve SECP384R1
    // /// Identifies this key as ECDSA (recommended SECP521R1) with SHA512 hashing
    // //#[serde(alias = "ECDSA_SHA512")]
    // ES512 = -36, // recommends curve SECP521R1
    // /// Identifies this key as RS256 aka RSASSA-PKCS1-v1_5 w/ SHA-256
    // RS256 = -257,
    // /// Identifies this key as RS384 aka RSASSA-PKCS1-v1_5 w/ SHA-384
    // RS384 = -258,
    // /// Identifies this key as RS512 aka RSASSA-PKCS1-v1_5 w/ SHA-512
    // RS512 = -259,
    // /// Identifies this key as PS256 aka RSASSA-PSS w/ SHA-256
    // PS256 = -37,
    // /// Identifies this key as PS384 aka RSASSA-PSS w/ SHA-384
    // PS384 = -38,
    // /// Identifies this key as PS512 aka RSASSA-PSS w/ SHA-512
    // PS512 = -39,
    // /// Identifies this key as EdDSA (likely curve ed25519)
    // EDDSA = -8,
    // /// Identifies this as an INSECURE RS1 aka RSASSA-PKCS1-v1_5 using SHA-1. This is not
    // /// used by validators, but can exist in some windows hello tpm's
    // INSECURE_RS1 = -65535,
    INSECURE_RS1 = -65535,             //  RSASSA-PKCS1-v1_5 using SHA-1
    RS512 = -259,                      //  RSASSA-PKCS1-v1_5 using SHA-512
    RS384 = -258,                      //  RSASSA-PKCS1-v1_5 using SHA-384
    RS256 = -257,                      //  RSASSA-PKCS1-v1_5 using SHA-256
    Ed448 = -53,                       //  EdDSA using the Ed448 parameter set
    ESP512 = -52,                      //  ECDSA using P-521 curve and SHA-512
    ESP384 = -51,                      //  ECDSA using P-384 curve and SHA-384
    ES256K = -47,                      //  ECDSA using secp256k1 curve and SHA-256
    HSS_LMS = -46,                     //  HSS/LMS hash-based digital signature
    SHAKE256 = -45,                    //  SHAKE-256 512-bit Hash Value
    SHA512 = -44,                      //  SHA-2 512-bit Hash
    SHA384 = -43,                      //  SHA-2 384-bit Hash
    RSAES_OAEP_SHA_512 = -42,          //  RSAES-OAEP w/ SHA-512
    RSAES_OAEP_SHA_256 = -41,          //  RSAES-OAEP w/ SHA-256
    RSAES_OAEP_RFC_8017_default = -40, //  RSAES-OAEP w/ SHA-1
    PS512 = -39,                       //  RSASSA-PSS w/ SHA-512
    PS384 = -38,                       //  RSASSA-PSS w/ SHA-384
    PS256 = -37,                       //  RSASSA-PSS w/ SHA-256
    ES512 = -36,                       //  ECDSA w/ SHA-512
    ES384 = -35,                       //  ECDSA w/ SHA-384
    ECDH_SS_A256KW = -34,              //  ECDH SS w/ Concat KDF and AES Key Wrap w/ 256-bit key
    ECDH_SS_A192KW = -33,              //  ECDH SS w/ Concat KDF and AES Key Wrap w/ 192-bit key
    ECDH_SS_A128KW = -32,              //  ECDH SS w/ Concat KDF and AES Key Wrap w/ 128-bit key
    ECDH_ES_A256KW = -31,              //  ECDH ES w/ Concat KDF and AES Key Wrap w/ 256-bit key
    ECDH_ES_A192KW = -30,              //  ECDH ES w/ Concat KDF and AES Key Wrap w/ 192-bit key
    ECDH_ES_A128KW = -29,              //  ECDH ES w/ Concat KDF and AES Key Wrap w/ 128-bit key
    ECDH_SS_HKDF512 = -28,             //  ECDH SS w/ HKDF - generate key directly
    ECDH_SS_HKDF256 = -27,             //  ECDH SS w/ HKDF - generate key directly
    ECDH_ES_HKDF512 = -26,             //  ECDH ES w/ HKDF - generate key directly
    ECDH_ES_HKDF256 = -25,             //  ECDH ES w/ HKDF - generate key directly
    Ed25519 = -19,                     //  EdDSA using the Ed25519 parameter set
    SHAKE128 = -18,                    //  SHAKE-128 256-bit Hash Value
    SHA512_256 = -17,                  //  SHA-2 512-bit Hash truncated to 256-bits
    SHA256 = -16,                      //  SHA-2 256-bit Hash
    SHA256_64 = -15,                   //  SHA-2 256-bit Hash truncated to 64-bits
    SHA1 = -14,                        //  SHA-1 Hash
    Direct_HKDF_AES256 = -13,          //  Shared secret w/ AES-MAC 256-bit key
    Direct_HKDF_AES128 = -12,          //  Shared secret w/ AES-MAC 128-bit key
    Direct_HKDF_SHA512 = -11,          //  Shared secret w/ HKDF and SHA-512
    Direct_HKDF_SHA256 = -10,          //  Shared secret w/ HKDF and SHA-256
    ESP256 = -9,                       //  ECDSA using P-256 curve and SHA-256
    EDDSA = -8,                        //  EdDSA
    ES256 = -7,                        //  ECDSA w/ SHA-256
    Direct = -6,                       //  Direct use of CEK
    A256KW = -5,                       //  AES Key Wrap w/ 256-bit key
    A192KW = -4,                       //  AES Key Wrap w/ 192-bit key
    A128KW = -3,                       //  AES Key Wrap w/ 128-bit key
    A128GCM = 1,                       //  AES-GCM mode w/ 128-bit key, 128-bit tag
    A192GCM = 2,                       //  AES-GCM mode w/ 192-bit key, 128-bit tag
    A256GCM = 3,                       //  AES-GCM mode w/ 256-bit key, 128-bit tag
    HMAC256_64 = 4,                    //  HMAC w/ SHA-256 truncated to 64 bits
    HMAC256_256 = 5,                   //  HMAC w/ SHA-256
    HMAC384_384 = 6,                   //  HMAC w/ SHA-384
    HMAC512_512 = 7,                   //  HMAC w/ SHA-512
    AES_CCM_16_64_128 = 10,            //  AES-CCM mode 128-bit key, 64-bit tag, 13-byte nonce
    AES_CCM_16_64_256 = 11,            //  AES-CCM mode 256-bit key, 64-bit tag, 13-byte nonce
    AES_CCM_64_64_128 = 12,            //  AES-CCM mode 128-bit key, 64-bit tag, 7-byte nonce
    AES_CCM_64_64_256 = 13,            //  AES-CCM mode 256-bit key, 64-bit tag, 7-byte nonce
    AES_MAC_128_64 = 14,               //  AES-MAC 128-bit key, 64-bit tag
    AES_MAC_256_64 = 15,               //  AES-MAC 256-bit key, 64-bit tag
    ChaCha20_Poly1305 = 24,            //  ChaCha20/Poly1305 w/ 256-bit key, 128-bit tag
    AES_MAC_128_128 = 25,              //  AES-MAC 128-bit key, 128-bit tag
    AES_MAC_256_128 = 26,              //  AES-MAC 256-bit key, 128-bit tag
    AES_CCM_16_128_128 = 30,           //  AES-CCM mode 128-bit key, 128-bit tag, 13-byte nonce
    AES_CCM_16_128_256 = 31,           //  AES-CCM mode 256-bit key, 128-bit tag, 13-byte nonce
    AES_CCM_64_128_128 = 32,           //  AES-CCM mode 128-bit key, 128-bit tag, 7-byte nonce
    AES_CCM_64_128_256 = 33,           //  AES-CCM mode 256-bit key, 128-bit tag, 7-byte nonce
    IV_GENERATION = 34,                //  For doing IV generation for symmetric algorithms.
}

impl Serialize for COSEAlgorithm {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_i64(*self as i64)
    }
}

impl<'de> Deserialize<'de> for COSEAlgorithm {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct COSEAlgorithmVisitor;

        impl<'de> Visitor<'de> for COSEAlgorithmVisitor {
            type Value = COSEAlgorithm;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a signed integer")
            }

            fn visit_i64<E>(self, v: i64) -> Result<Self::Value, E>
            where
                E: SerdeError,
            {
                COSEAlgorithm::try_from(v).map_err(|_| {
                    SerdeError::invalid_value(Unexpected::Signed(v), &"valid COSEAlgorithm")
                })
            }
        }

        deserializer.deserialize_any(COSEAlgorithmVisitor)
    }
}

impl TryFrom<i64> for COSEAlgorithm {
    type Error = CryptoError;
    fn try_from(i: i64) -> Result<Self, Self::Error> {
        match i {
            i if i == COSEAlgorithm::RS512 as i64 => Ok(COSEAlgorithm::RS512),
            i if i == COSEAlgorithm::RS384 as i64 => Ok(COSEAlgorithm::RS384),
            i if i == COSEAlgorithm::RS256 as i64 => Ok(COSEAlgorithm::RS256),
            i if i == COSEAlgorithm::Ed448 as i64 => Ok(COSEAlgorithm::Ed448),
            i if i == COSEAlgorithm::ESP512 as i64 => Ok(COSEAlgorithm::ESP512),
            i if i == COSEAlgorithm::ESP384 as i64 => Ok(COSEAlgorithm::ESP384),
            i if i == COSEAlgorithm::ES256K as i64 => Ok(COSEAlgorithm::ES256K),
            i if i == COSEAlgorithm::HSS_LMS as i64 => Ok(COSEAlgorithm::HSS_LMS),
            i if i == COSEAlgorithm::SHAKE256 as i64 => Ok(COSEAlgorithm::SHAKE256),
            i if i == COSEAlgorithm::SHA512 as i64 => Ok(COSEAlgorithm::SHA512),
            i if i == COSEAlgorithm::SHA384 as i64 => Ok(COSEAlgorithm::SHA384),
            i if i == COSEAlgorithm::RSAES_OAEP_SHA_512 as i64 => {
                Ok(COSEAlgorithm::RSAES_OAEP_SHA_512)
            }
            i if i == COSEAlgorithm::RSAES_OAEP_SHA_256 as i64 => {
                Ok(COSEAlgorithm::RSAES_OAEP_SHA_256)
            }
            i if i == COSEAlgorithm::RSAES_OAEP_RFC_8017_default as i64 => {
                Ok(COSEAlgorithm::RSAES_OAEP_RFC_8017_default)
            }
            i if i == COSEAlgorithm::PS512 as i64 => Ok(COSEAlgorithm::PS512),
            i if i == COSEAlgorithm::PS384 as i64 => Ok(COSEAlgorithm::PS384),
            i if i == COSEAlgorithm::PS256 as i64 => Ok(COSEAlgorithm::PS256),
            i if i == COSEAlgorithm::ES512 as i64 => Ok(COSEAlgorithm::ES512),
            i if i == COSEAlgorithm::ES384 as i64 => Ok(COSEAlgorithm::ES384),
            i if i == COSEAlgorithm::ECDH_SS_A256KW as i64 => Ok(COSEAlgorithm::ECDH_SS_A256KW),
            i if i == COSEAlgorithm::ECDH_SS_A192KW as i64 => Ok(COSEAlgorithm::ECDH_SS_A192KW),
            i if i == COSEAlgorithm::ECDH_SS_A128KW as i64 => Ok(COSEAlgorithm::ECDH_SS_A128KW),
            i if i == COSEAlgorithm::ECDH_ES_A256KW as i64 => Ok(COSEAlgorithm::ECDH_ES_A256KW),
            i if i == COSEAlgorithm::ECDH_ES_A192KW as i64 => Ok(COSEAlgorithm::ECDH_ES_A192KW),
            i if i == COSEAlgorithm::ECDH_ES_A128KW as i64 => Ok(COSEAlgorithm::ECDH_ES_A128KW),
            i if i == COSEAlgorithm::ECDH_SS_HKDF512 as i64 => Ok(COSEAlgorithm::ECDH_SS_HKDF512),
            i if i == COSEAlgorithm::ECDH_SS_HKDF256 as i64 => Ok(COSEAlgorithm::ECDH_SS_HKDF256),
            i if i == COSEAlgorithm::ECDH_ES_HKDF512 as i64 => Ok(COSEAlgorithm::ECDH_ES_HKDF512),
            i if i == COSEAlgorithm::ECDH_ES_HKDF256 as i64 => Ok(COSEAlgorithm::ECDH_ES_HKDF256),
            i if i == COSEAlgorithm::Ed25519 as i64 => Ok(COSEAlgorithm::Ed25519),
            i if i == COSEAlgorithm::SHAKE128 as i64 => Ok(COSEAlgorithm::SHAKE128),
            i if i == COSEAlgorithm::SHA512_256 as i64 => Ok(COSEAlgorithm::SHA512_256),
            i if i == COSEAlgorithm::SHA256 as i64 => Ok(COSEAlgorithm::SHA256),
            i if i == COSEAlgorithm::SHA256_64 as i64 => Ok(COSEAlgorithm::SHA256_64),
            i if i == COSEAlgorithm::SHA1 as i64 => Ok(COSEAlgorithm::SHA1),
            i if i == COSEAlgorithm::Direct_HKDF_AES256 as i64 => {
                Ok(COSEAlgorithm::Direct_HKDF_AES256)
            }
            i if i == COSEAlgorithm::Direct_HKDF_AES128 as i64 => {
                Ok(COSEAlgorithm::Direct_HKDF_AES128)
            }
            i if i == COSEAlgorithm::Direct_HKDF_SHA512 as i64 => {
                Ok(COSEAlgorithm::Direct_HKDF_SHA512)
            }
            i if i == COSEAlgorithm::Direct_HKDF_SHA256 as i64 => {
                Ok(COSEAlgorithm::Direct_HKDF_SHA256)
            }
            i if i == COSEAlgorithm::ESP256 as i64 => Ok(COSEAlgorithm::ESP256),
            i if i == COSEAlgorithm::EDDSA as i64 => Ok(COSEAlgorithm::EDDSA),
            i if i == COSEAlgorithm::ES256 as i64 => Ok(COSEAlgorithm::ES256),
            i if i == COSEAlgorithm::Direct as i64 => Ok(COSEAlgorithm::Direct),
            i if i == COSEAlgorithm::A256KW as i64 => Ok(COSEAlgorithm::A256KW),
            i if i == COSEAlgorithm::A192KW as i64 => Ok(COSEAlgorithm::A192KW),
            i if i == COSEAlgorithm::A128KW as i64 => Ok(COSEAlgorithm::A128KW),
            i if i == COSEAlgorithm::A128GCM as i64 => Ok(COSEAlgorithm::A128GCM),
            i if i == COSEAlgorithm::A192GCM as i64 => Ok(COSEAlgorithm::A192GCM),
            i if i == COSEAlgorithm::A256GCM as i64 => Ok(COSEAlgorithm::A256GCM),
            i if i == COSEAlgorithm::HMAC256_64 as i64 => Ok(COSEAlgorithm::HMAC256_64),
            i if i == COSEAlgorithm::HMAC256_256 as i64 => Ok(COSEAlgorithm::HMAC256_256),
            i if i == COSEAlgorithm::HMAC384_384 as i64 => Ok(COSEAlgorithm::HMAC384_384),
            i if i == COSEAlgorithm::HMAC512_512 as i64 => Ok(COSEAlgorithm::HMAC512_512),
            i if i == COSEAlgorithm::AES_CCM_16_64_128 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_16_64_128)
            }
            i if i == COSEAlgorithm::AES_CCM_16_64_256 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_16_64_256)
            }
            i if i == COSEAlgorithm::AES_CCM_64_64_128 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_64_64_128)
            }
            i if i == COSEAlgorithm::AES_CCM_64_64_256 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_64_64_256)
            }
            i if i == COSEAlgorithm::AES_MAC_128_64 as i64 => Ok(COSEAlgorithm::AES_MAC_128_64),
            i if i == COSEAlgorithm::AES_MAC_256_64 as i64 => Ok(COSEAlgorithm::AES_MAC_256_64),
            i if i == COSEAlgorithm::ChaCha20_Poly1305 as i64 => {
                Ok(COSEAlgorithm::ChaCha20_Poly1305)
            }
            i if i == COSEAlgorithm::AES_MAC_128_128 as i64 => Ok(COSEAlgorithm::AES_MAC_128_128),
            i if i == COSEAlgorithm::AES_MAC_256_128 as i64 => Ok(COSEAlgorithm::AES_MAC_256_128),
            i if i == COSEAlgorithm::AES_CCM_16_128_128 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_16_128_128)
            }
            i if i == COSEAlgorithm::AES_CCM_16_128_256 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_16_128_256)
            }
            i if i == COSEAlgorithm::AES_CCM_64_128_128 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_64_128_128)
            }
            i if i == COSEAlgorithm::AES_CCM_64_128_256 as i64 => {
                Ok(COSEAlgorithm::AES_CCM_64_128_256)
            }
            i if i == COSEAlgorithm::IV_GENERATION as i64 => Ok(COSEAlgorithm::IV_GENERATION),
            i if i == COSEAlgorithm::INSECURE_RS1 as i64 => Ok(COSEAlgorithm::INSECURE_RS1),
            _ => Err(CryptoError::UnknownAlgorithm),
        }
    }
}

/// A COSE Elliptic Curve Public Key. This is generally the provided credential
/// that an authenticator registers, and is used to authenticate the user.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct COSEEC2Key {
    /// The curve that this key references.
    pub curve: Curve,
    /// The key's public X coordinate.
    pub x: Vec<u8>,
    /// The key's public Y coordinate.
    pub y: Vec<u8>,
}

impl COSEEC2Key {
    // The SEC 1 uncompressed point format is "0x04 || x coordinate || y coordinate".
    // See Section 2.3.3 of "SEC 1: Elliptic Curve Cryptography" https://www.secg.org/sec1-v2.pdf.
    pub fn from_sec1_uncompressed(curve: Curve, key: &[u8]) -> Result<Self, CryptoError> {
        if !(curve == Curve::SECP256R1 && key.len() == 65) {
            return Err(CryptoError::UnsupportedCurve(curve));
        }
        if key[0] != 0x04 {
            return Err(CryptoError::MalformedInput);
        }
        let key = &key[1..];
        let (x, y) = key.split_at(key.len() / 2);
        Ok(COSEEC2Key {
            curve,
            x: x.to_vec(),
            y: y.to_vec(),
        })
    }

    pub fn der_spki(&self) -> Result<Vec<u8>, CryptoError> {
        if self.curve != Curve::SECP256R1 {
            return Err(CryptoError::UnsupportedCurve(self.curve));
        }

        // SubjectPublicKeyInfo
        der::sequence(&[
            // algorithm: AlgorithmIdentifier
            &der::sequence(&[
                // algorithm
                &der::object_id(der::OID_EC_PUBLIC_KEY_BYTES)?,
                // parameters
                &der::object_id(der::OID_SECP256R1_BYTES)?,
            ])?,
            // subjectPublicKey
            &der::bit_string(
                // SEC 1 uncompressed format
                &[&[0x04], self.x.as_slice(), self.y.as_slice()].concat(),
            )?,
        ])
    }
}

/// A Octet Key Pair (OKP).
/// The other version uses only the x-coordinate as the y-coordinate is
/// either to be recomputed or not needed for the key agreement operation ('OKP').
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct COSEOKPKey {
    /// The curve that this key references.
    pub curve: Curve,
    /// The key's public X coordinate.
    pub x: Vec<u8>,
}

impl COSEOKPKey {
    pub fn der_spki(&self) -> Result<Vec<u8>, CryptoError> {
        if self.curve != Curve::Ed25519 {
            return Err(CryptoError::UnsupportedCurve(self.curve));
        }

        // SubjectPublicKeyInfo
        der::sequence(&[
            // algorithm: AlgorithmIdentifier
            &der::sequence(&[
                // algorithm
                &der::object_id(der::OID_ED25519_BYTES)?,
                // parameters
                // (absent as per RFC 8410)
            ])?,
            // subjectPublicKey
            &der::bit_string(
                // RFC 8410
                self.x.as_slice(),
            )?,
        ])
    }
}

/// A COSE RSA PublicKey. This is a provided credential from a registered authenticator.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct COSERSAKey {
    /// An RSA modulus
    pub n: Vec<u8>,
    /// An RSA exponent
    pub e: Vec<u8>,
}

impl COSERSAKey {
    pub fn der_spki(&self) -> Result<Vec<u8>, CryptoError> {
        // SubjectPublicKeyInfo
        der::sequence(&[
            // algorithm: AlgorithmIdentifier
            &der::sequence(&[
                // algorithm
                &der::object_id(der::OID_RSA_ENCRYPTION_BYTES)?,
                // parameters
                &der::null()?,
            ])?,
            // subjectPublicKey
            &der::bit_string(
                // RFC 4055 RSAPublicKey
                &der::sequence(&[&der::integer(&self.n)?, &der::integer(&self.e)?])?,
            )?,
        ])
    }
}

// https://tools.ietf.org/html/rfc8152#section-13
#[allow(non_camel_case_types)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum COSEKeyTypeId {
    // Reserved is invalid
    // Reserved = 0,
    /// Octet Key Pair
    OKP = 1,
    /// Elliptic Curve Keys w/ x- and y-coordinate
    EC2 = 2,
    /// RSA
    RSA = 3,
}

impl Serialize for COSEKeyTypeId {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_i64(*self as i64)
    }
}

impl TryFrom<i64> for COSEKeyTypeId {
    type Error = CryptoError;
    fn try_from(i: i64) -> Result<Self, Self::Error> {
        match i {
            i if i == COSEKeyTypeId::OKP as i64 => Ok(COSEKeyTypeId::OKP),
            i if i == COSEKeyTypeId::EC2 as i64 => Ok(COSEKeyTypeId::EC2),
            i if i == COSEKeyTypeId::RSA as i64 => Ok(COSEKeyTypeId::RSA),
            _ => Err(CryptoError::UnknownKeyType),
        }
    }
}

/// The type of Key contained within a COSE value. You should never need
/// to alter or change this type.
#[allow(non_camel_case_types)]
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum COSEKeyType {
    /// Identifies this as an Elliptic Curve EC2 key
    EC2(COSEEC2Key),
    /// Identifies this as an Elliptic Curve octet key pair
    OKP(COSEOKPKey),
    /// Identifies this as an RSA key
    RSA(COSERSAKey),
}

/// A COSE Key as provided by the Authenticator. You should never need
/// to alter or change these values.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct COSEKey {
    /// COSE signature algorithm, indicating the type of key and hash type
    /// that should be used.
    pub alg: COSEAlgorithm,
    /// The public key
    pub key: COSEKeyType,
}

impl COSEKey {
    /// Generates a new key pair for the specified algorithm.
    /// Returns an PKCS#8 encoding of the private key, and the public key as a COSEKey.
    pub fn generate(alg: COSEAlgorithm) -> Result<(Vec<u8>, Self), CryptoError> {
        if alg != COSEAlgorithm::ES256 && alg != COSEAlgorithm::ECDH_ES_HKDF256 {
            return Err(CryptoError::UnsupportedAlgorithm(alg));
        }
        let (private, public) = gen_p256()?;
        let cose_ec2_key = COSEEC2Key::from_sec1_uncompressed(Curve::SECP256R1, &public)?;
        let public = COSEKey {
            alg,
            key: COSEKeyType::EC2(cose_ec2_key),
        };
        Ok((private, public))
    }

    pub fn der_spki(&self) -> Result<Vec<u8>, CryptoError> {
        match &self.key {
            COSEKeyType::EC2(ec2_key) => ec2_key.der_spki(),
            COSEKeyType::OKP(okp_key) => okp_key.der_spki(),
            COSEKeyType::RSA(rsa_key) => rsa_key.der_spki(),
        }
    }
}

impl<'de> Deserialize<'de> for COSEKey {
    fn deserialize<D>(deserializer: D) -> std::result::Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct COSEKeyVisitor;

        impl<'de> Visitor<'de> for COSEKeyVisitor {
            type Value = COSEKey;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a map")
            }

            fn visit_map<M>(self, mut map: M) -> std::result::Result<Self::Value, M::Error>
            where
                M: MapAccess<'de>,
            {
                let mut key_type: Option<COSEKeyTypeId> = None;
                let mut alg: Option<COSEAlgorithm> = None;
                // OKP / EC2
                let mut curve: Option<Curve> = None;
                let mut x: Option<Vec<u8>> = None;
                let mut y: Option<Vec<u8>> = None;

                // RSA specific
                let mut n: Option<Vec<u8>> = None;
                let mut e: Option<Vec<u8>> = None;

                while let Some(key) = map.next_key()? {
                    // See https://www.iana.org/assignments/cose/cose.xhtml#key-type-parameters
                    match key {
                        1 => {
                            if key_type.is_some() {
                                return Err(SerdeError::duplicate_field("key_type"));
                            }
                            let value: i64 = map.next_value()?;
                            let val = COSEKeyTypeId::try_from(value).map_err(|_| {
                                SerdeError::custom(format!("unsupported key_type {value}"))
                            })?;
                            key_type = Some(val);
                        }
                        3 => {
                            if alg.is_some() {
                                return Err(SerdeError::duplicate_field("alg"));
                            }
                            let value: i64 = map.next_value()?;
                            let val = COSEAlgorithm::try_from(value).map_err(|_| {
                                SerdeError::custom(format!("unsupported algorithm {value}"))
                            })?;
                            alg = Some(val);
                        }
                        -1 => match key_type {
                            None => return Err(SerdeError::missing_field("key_type")),
                            Some(COSEKeyTypeId::OKP) | Some(COSEKeyTypeId::EC2) => {
                                if curve.is_some() {
                                    return Err(SerdeError::duplicate_field("curve"));
                                }
                                let value: i64 = map.next_value()?;
                                let val = Curve::try_from(value).map_err(|_| {
                                    SerdeError::custom(format!("unsupported curve {value}"))
                                })?;
                                curve = Some(val);
                            }
                            Some(COSEKeyTypeId::RSA) => {
                                if n.is_some() {
                                    return Err(SerdeError::duplicate_field("n"));
                                }
                                let value: ByteBuf = map.next_value()?;
                                n = Some(value.to_vec());
                            }
                        },
                        -2 => match key_type {
                            None => return Err(SerdeError::missing_field("key_type")),
                            Some(COSEKeyTypeId::OKP) | Some(COSEKeyTypeId::EC2) => {
                                if x.is_some() {
                                    return Err(SerdeError::duplicate_field("x"));
                                }
                                let value: ByteBuf = map.next_value()?;
                                x = Some(value.to_vec());
                            }
                            Some(COSEKeyTypeId::RSA) => {
                                if e.is_some() {
                                    return Err(SerdeError::duplicate_field("e"));
                                }
                                let value: ByteBuf = map.next_value()?;
                                e = Some(value.to_vec());
                            }
                        },
                        -3 if key_type == Some(COSEKeyTypeId::EC2) => {
                            if y.is_some() {
                                return Err(SerdeError::duplicate_field("y"));
                            }
                            let value: ByteBuf = map.next_value()?;
                            y = Some(value.to_vec());
                        }
                        other => {
                            return Err(SerdeError::custom(format!("unexpected field: {other}")));
                        }
                    };
                }

                let key_type = key_type.ok_or_else(|| SerdeError::missing_field("key_type (1)"))?;
                let alg = alg.ok_or_else(|| SerdeError::missing_field("alg (3)"))?;

                let res = match key_type {
                    COSEKeyTypeId::OKP => {
                        let curve = curve.ok_or_else(|| SerdeError::missing_field("curve (-1)"))?;
                        let x = x.ok_or_else(|| SerdeError::missing_field("x (-2)"))?;
                        COSEKeyType::OKP(COSEOKPKey { curve, x })
                    }
                    COSEKeyTypeId::EC2 => {
                        let curve = curve.ok_or_else(|| SerdeError::missing_field("curve (-1)"))?;
                        let x = x.ok_or_else(|| SerdeError::missing_field("x (-2)"))?;
                        let y = y.ok_or_else(|| SerdeError::missing_field("y (-3)"))?;
                        COSEKeyType::EC2(COSEEC2Key { curve, x, y })
                    }
                    COSEKeyTypeId::RSA => {
                        let n = n.ok_or_else(|| SerdeError::missing_field("n (-1)"))?;
                        let e = e.ok_or_else(|| SerdeError::missing_field("e (-2)"))?;
                        COSEKeyType::RSA(COSERSAKey { e, n })
                    }
                };
                Ok(COSEKey { alg, key: res })
            }
        }

        deserializer.deserialize_bytes(COSEKeyVisitor)
    }
}

impl Serialize for COSEKey {
    fn serialize<S>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match &self.key {
            COSEKeyType::OKP(key) => {
                serialize_map!(
                    serializer,
                    &1 => &COSEKeyTypeId::OKP,
                    &3 => &self.alg,
                    &-1 => &key.curve,
                    &-2 => &serde_bytes::Bytes::new(&key.x),
                )
            }
            COSEKeyType::EC2(key) => {
                serialize_map!(
                    serializer,
                    &1 => &COSEKeyTypeId::EC2,
                    &3 => &self.alg,
                    &-1 => &key.curve,
                    &-2 => &serde_bytes::Bytes::new(&key.x),
                    &-3 => &serde_bytes::Bytes::new(&key.y),
                )
            }
            COSEKeyType::RSA(key) => {
                serialize_map!(
                    serializer,
                    &1 => &COSEKeyTypeId::RSA,
                    &3 => &self.alg,
                    &-1 => &serde_bytes::Bytes::new(&key.n),
                    &-2 => &serde_bytes::Bytes::new(&key.e),
                )
            }
        }
    }
}

/// Errors that can be returned from COSE functions.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub enum CryptoError {
    // DecodingFailure,
    LibraryFailure,
    MalformedInput,
    // MissingHeader,
    // UnexpectedHeaderValue,
    // UnexpectedTag,
    // UnexpectedType,
    // Unimplemented,
    // VerificationFailed,
    // SigningFailed,
    // InvalidArgument,
    UnknownKeyType,
    UnknownSignatureScheme,
    UnknownAlgorithm,
    WrongSaltLength,
    UnsupportedAlgorithm(COSEAlgorithm),
    UnsupportedCurve(Curve),
    UnsupportedKeyType,
    Backend(String),
}

impl From<CryptoError> for CommandError {
    fn from(e: CryptoError) -> Self {
        CommandError::Crypto(e)
    }
}

impl From<CryptoError> for AuthenticatorError {
    fn from(e: CryptoError) -> Self {
        AuthenticatorError::HIDError(HIDError::Command(CommandError::Crypto(e)))
    }
}

pub struct U2FRegisterAnswer<'a> {
    pub certificate: &'a [u8],
    pub signature: &'a [u8],
}

// We will only return MalformedInput here
pub fn parse_u2f_der_certificate(data: &[u8]) -> Result<U2FRegisterAnswer<'_>, CryptoError> {
    // So we don't panic below, when accessing individual bytes
    if data.len() < 4 {
        return Err(CryptoError::MalformedInput);
    }
    // Check if it is a SEQUENCE
    if data[0] != 0x30 {
        return Err(CryptoError::MalformedInput);
    }

    // This algorithm is taken from mozilla-central/security/nss/lib/mozpkix/lib/pkixder.cpp
    // The short form of length is a single byte with the high order bit set
    // to zero. The long form of length is one byte with the high order bit
    // set, followed by N bytes, where N is encoded in the lowest 7 bits of
    // the first byte.
    let end = if (data[1] & 0x80) == 0 {
        2 + data[1] as usize
    } else if data[1] == 0x81 {
        // The next byte specifies the length

        if data[2] < 128 {
            // Not shortest possible encoding
            // Forbidden by DER-format
            return Err(CryptoError::MalformedInput);
        }
        3 + data[2] as usize
    } else if data[1] == 0x82 {
        // The next 2 bytes specify the length
        let l = u16::from_be_bytes([data[2], data[3]]);
        if l < 256 {
            // Not shortest possible encoding
            // Forbidden by DER-format
            return Err(CryptoError::MalformedInput);
        }
        4 + l as usize
    } else {
        // We don't support lengths larger than 2^16 - 1.
        return Err(CryptoError::MalformedInput);
    };

    if data.len() < end {
        return Err(CryptoError::MalformedInput);
    }

    Ok(U2FRegisterAnswer {
        certificate: &data[0..end],
        signature: &data[end..],
    })
}

#[cfg(test)]
mod test {
    use std::convert::TryFrom;

    use super::backend::{ecdsa_p256_sha256_sign_raw, test_ecdsa_p256_sha256_verify_raw};
    use super::{
        backend::hmac_sha256, backend::sha256, backend::test_ecdh_p256_raw, COSEAlgorithm, COSEKey,
        Curve, PinProtocolImpl, PinUvAuth1, PinUvAuth2, PinUvAuthProtocol, PublicInputs,
        SharedSecret,
    };
    use crate::crypto::{COSEEC2Key, COSEKeyType, COSEOKPKey, COSERSAKey};
    use crate::ctap2::attestation::AAGuid;
    use crate::ctap2::commands::client_pin::Pin;
    use crate::ctap2::commands::get_info::{
        tests::AAGUID_RAW, AuthenticatorOptions, AuthenticatorVersion,
    };
    use crate::util::decode_hex;
    use crate::AuthenticatorInfo;
    use serde_cbor::de::from_slice;

    // Extracted from RFC 8410 Example 10.1
    const SAMPLE_ED25519_KEY: &[u8] = &[
        0x19, 0xbf, 0x44, 0x09, 0x69, 0x84, 0xcd, 0xfe, 0x85, 0x41, 0xba, 0xc1, 0x67, 0xdc, 0x3b,
        0x96, 0xc8, 0x50, 0x86, 0xaa, 0x30, 0xb6, 0xb6, 0xcb, 0x0c, 0x5c, 0x38, 0xad, 0x70, 0x31,
        0x66, 0xe1,
    ];

    const SAMPLE_P256_X: &[u8] = &[
        0xfc, 0x9e, 0xd3, 0x6f, 0x7c, 0x1a, 0xa9, 0x15, 0xce, 0x3e, 0xa1, 0x77, 0xf0, 0x75, 0x67,
        0xf0, 0x7f, 0x16, 0xf9, 0x47, 0x9d, 0x95, 0xad, 0x8e, 0xd4, 0x97, 0x1d, 0x33, 0x05, 0xe3,
        0x1a, 0x80,
    ];
    const SAMPLE_P256_Y: &[u8] = &[
        0x50, 0xb7, 0x33, 0xaf, 0x8c, 0x0b, 0x0e, 0xe1, 0xda, 0x8d, 0xe0, 0xac, 0xf9, 0xd8, 0xe1,
        0x32, 0x82, 0xf0, 0x63, 0xb7, 0xb3, 0x0d, 0x73, 0xd4, 0xd3, 0x2c, 0x9a, 0xad, 0x6d, 0xfa,
        0x8b, 0x27,
    ];

    const SAMPLE_RSA_MODULUS: &[u8] = &[
        0xd4, 0xd2, 0x53, 0xed, 0x7a, 0x69, 0xb1, 0x84, 0xc9, 0xfb, 0x70, 0x30, 0x0c, 0x51, 0xb1,
        0x8f, 0x89, 0x6c, 0xb1, 0x31, 0x6d, 0x87, 0xbe, 0xe1, 0xc7, 0xf7, 0xb0, 0x4f, 0xe7, 0x27,
        0xa7, 0xb7, 0x7c, 0x55, 0x20, 0x37, 0xa8, 0xac, 0x40, 0xf4, 0xbc, 0x59, 0xc4, 0x92, 0x8f,
        0x13, 0x5b, 0x5e, 0xa7, 0x18, 0x05, 0xcc, 0xd7, 0x9c, 0xfb, 0x88, 0x6c, 0xf1, 0xbc, 0x6b,
        0x1b, 0x8d, 0xb7, 0x8d, 0x2d, 0xaa, 0xcb, 0xee, 0xdb, 0xab, 0x49, 0x36, 0x77, 0xe5, 0xd1,
        0x84, 0xa1, 0x40, 0x3f, 0xf6, 0xf7, 0x98, 0x6c, 0xaa, 0x24, 0x48, 0x30, 0x44, 0xdc, 0x68,
        0xbd, 0x9e, 0x74, 0x37, 0xaf, 0x27, 0x12, 0x90, 0x74, 0x0d, 0x9e, 0x3c, 0xa5, 0x3a, 0x1d,
        0xb8, 0x54, 0x92, 0xd4, 0x6d, 0x1f, 0xf9, 0x39, 0xb8, 0x1d, 0x8a, 0x5e, 0xbe, 0x12, 0xbd,
        0xe2, 0x9c, 0xf2, 0x5a, 0x48, 0x5d, 0x71, 0x2c, 0x71, 0x72, 0x6d, 0xd2, 0xcb, 0x37, 0xb1,
        0xe6, 0x2f, 0x76, 0x43, 0xda, 0xca, 0x44, 0x30, 0x7b, 0x28, 0xe7, 0xe4, 0xec, 0xa9, 0xc9,
        0x1a, 0x5f, 0xe5, 0x51, 0x03, 0x25, 0x60, 0x7c, 0x5a, 0x69, 0x12, 0x4d, 0x50, 0xfd, 0xb2,
        0xb8, 0x6e, 0x13, 0xb2, 0x92, 0xda, 0x0e, 0x31, 0xc9, 0xf1, 0x9c, 0xde, 0x17, 0x63, 0xe4,
        0xcb, 0xac, 0xd5, 0xee, 0x84, 0x06, 0xde, 0x67, 0x2d, 0xb8, 0xd2, 0xe1, 0x4b, 0xbb, 0x49,
        0xea, 0x45, 0xd4, 0xa1, 0x7f, 0x46, 0xf2, 0xd6, 0x0c, 0x05, 0x9d, 0x1d, 0x1a, 0x99, 0x41,
        0x20, 0x5e, 0x1a, 0xa4, 0xcc, 0x21, 0x44, 0x58, 0x8b, 0xcd, 0x98, 0xe4, 0x3d, 0x53, 0x20,
        0xfc, 0xfc, 0x7b, 0x9f, 0x43, 0x35, 0xfb, 0x38, 0x37, 0x23, 0xd0, 0x76, 0xe3, 0x3d, 0x4f,
        0x89, 0x9b, 0x89, 0x32, 0x81, 0x89, 0xed, 0x58, 0xc0, 0x80, 0x18, 0x83, 0x5b, 0xaf, 0x5a,
        0xa5,
    ];

    #[test]
    fn test_rsa_key_to_der_spki() {
        // $ ascii2der | xxd -i
        // SEQUENCE {
        //   SEQUENCE {
        //     # rsaEncryption
        //     OBJECT_IDENTIFIER { 1.2.840.113549.1.1.1 }
        //     NULL {}
        //   }
        //   BIT_STRING {
        //     `00`
        //     SEQUENCE {
        //       INTEGER { `00d4d253ed7a69b184c9fb70300c51b18f896cb1316d87bee1c7f7b04fe727a7b77c552037a8ac40f4bc59c4928f135b5ea71805ccd79cfb886cf1bc6b1b8db78d2daacbeedbab493677e5d184a1403ff6f7986caa24483044dc68bd9e7437af271290740d9e3ca53a1db85492d46d1ff939b81d8a5ebe12bde29cf25a485d712c71726dd2cb37b1e62f7643daca44307b28e7e4eca9c91a5fe5510325607c5a69124d50fdb2b86e13b292da0e31c9f19cde1763e4cbacd5ee8406de672db8d2e14bbb49ea45d4a17f46f2d60c059d1d1a9941205e1aa4cc2144588bcd98e43d5320fcfc7b9f4335fb383723d076e33d4f899b89328189ed58c08018835baf5aa5` }
        //       INTEGER { 65537 }
        //     }
        //   }
        // }
        let expected: &[u8] = &[
            0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
            0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a,
            0x02, 0x82, 0x01, 0x01, 0x00, 0xd4, 0xd2, 0x53, 0xed, 0x7a, 0x69, 0xb1, 0x84, 0xc9,
            0xfb, 0x70, 0x30, 0x0c, 0x51, 0xb1, 0x8f, 0x89, 0x6c, 0xb1, 0x31, 0x6d, 0x87, 0xbe,
            0xe1, 0xc7, 0xf7, 0xb0, 0x4f, 0xe7, 0x27, 0xa7, 0xb7, 0x7c, 0x55, 0x20, 0x37, 0xa8,
            0xac, 0x40, 0xf4, 0xbc, 0x59, 0xc4, 0x92, 0x8f, 0x13, 0x5b, 0x5e, 0xa7, 0x18, 0x05,
            0xcc, 0xd7, 0x9c, 0xfb, 0x88, 0x6c, 0xf1, 0xbc, 0x6b, 0x1b, 0x8d, 0xb7, 0x8d, 0x2d,
            0xaa, 0xcb, 0xee, 0xdb, 0xab, 0x49, 0x36, 0x77, 0xe5, 0xd1, 0x84, 0xa1, 0x40, 0x3f,
            0xf6, 0xf7, 0x98, 0x6c, 0xaa, 0x24, 0x48, 0x30, 0x44, 0xdc, 0x68, 0xbd, 0x9e, 0x74,
            0x37, 0xaf, 0x27, 0x12, 0x90, 0x74, 0x0d, 0x9e, 0x3c, 0xa5, 0x3a, 0x1d, 0xb8, 0x54,
            0x92, 0xd4, 0x6d, 0x1f, 0xf9, 0x39, 0xb8, 0x1d, 0x8a, 0x5e, 0xbe, 0x12, 0xbd, 0xe2,
            0x9c, 0xf2, 0x5a, 0x48, 0x5d, 0x71, 0x2c, 0x71, 0x72, 0x6d, 0xd2, 0xcb, 0x37, 0xb1,
            0xe6, 0x2f, 0x76, 0x43, 0xda, 0xca, 0x44, 0x30, 0x7b, 0x28, 0xe7, 0xe4, 0xec, 0xa9,
            0xc9, 0x1a, 0x5f, 0xe5, 0x51, 0x03, 0x25, 0x60, 0x7c, 0x5a, 0x69, 0x12, 0x4d, 0x50,
            0xfd, 0xb2, 0xb8, 0x6e, 0x13, 0xb2, 0x92, 0xda, 0x0e, 0x31, 0xc9, 0xf1, 0x9c, 0xde,
            0x17, 0x63, 0xe4, 0xcb, 0xac, 0xd5, 0xee, 0x84, 0x06, 0xde, 0x67, 0x2d, 0xb8, 0xd2,
            0xe1, 0x4b, 0xbb, 0x49, 0xea, 0x45, 0xd4, 0xa1, 0x7f, 0x46, 0xf2, 0xd6, 0x0c, 0x05,
            0x9d, 0x1d, 0x1a, 0x99, 0x41, 0x20, 0x5e, 0x1a, 0xa4, 0xcc, 0x21, 0x44, 0x58, 0x8b,
            0xcd, 0x98, 0xe4, 0x3d, 0x53, 0x20, 0xfc, 0xfc, 0x7b, 0x9f, 0x43, 0x35, 0xfb, 0x38,
            0x37, 0x23, 0xd0, 0x76, 0xe3, 0x3d, 0x4f, 0x89, 0x9b, 0x89, 0x32, 0x81, 0x89, 0xed,
            0x58, 0xc0, 0x80, 0x18, 0x83, 0x5b, 0xaf, 0x5a, 0xa5, 0x02, 0x03, 0x01, 0x00, 0x01,
        ];
        let rsa_key = COSERSAKey {
            e: vec![1, 0, 1],
            n: SAMPLE_RSA_MODULUS.to_vec(),
        };
        let cose_key: COSEKey = COSEKey {
            alg: COSEAlgorithm::RS256,
            key: COSEKeyType::RSA(rsa_key),
        };
        let actual = cose_key.der_spki().expect("Failed to serialize to SPKI");
        assert_eq!(expected, &actual);
    }

    #[test]
    fn test_rsa_key_to_cbor() {
        let key = COSERSAKey {
            e: vec![1, 0, 1],
            n: SAMPLE_RSA_MODULUS.to_vec(),
        };
        let cose_key: COSEKey = COSEKey {
            alg: COSEAlgorithm::RS256,
            key: COSEKeyType::RSA(key),
        };
        let cose_key_cbor = serde_cbor::to_vec(&cose_key).expect("Failed to serialize key");
        let actual = serde_cbor::from_slice(&cose_key_cbor).expect("Failed to deserialize key");
        assert_eq!(cose_key, actual);
    }

    #[test]
    fn test_ec2_key_to_der_spki() {
        // $ ascii2der | xxd -i
        // SEQUENCE {
        //   SEQUENCE {
        //     # ecPublicKey
        //     OBJECT_IDENTIFIER { 1.2.840.10045.2.1 }
        //     # secp256r1
        //     OBJECT_IDENTIFIER { 1.2.840.10045.3.1.7 }
        //   }
        //   BIT_STRING { `00` `04fc9ed36f7c1aa915ce3ea177f07567f07f16f9479d95ad8ed4971d3305e31a8050b733af8c0b0ee1da8de0acf9d8e13282f063b7b30d73d4d32c9aad6dfa8b27` }
        // }
        let expected = [
            0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
            0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0xfc,
            0x9e, 0xd3, 0x6f, 0x7c, 0x1a, 0xa9, 0x15, 0xce, 0x3e, 0xa1, 0x77, 0xf0, 0x75, 0x67,
            0xf0, 0x7f, 0x16, 0xf9, 0x47, 0x9d, 0x95, 0xad, 0x8e, 0xd4, 0x97, 0x1d, 0x33, 0x05,
            0xe3, 0x1a, 0x80, 0x50, 0xb7, 0x33, 0xaf, 0x8c, 0x0b, 0x0e, 0xe1, 0xda, 0x8d, 0xe0,
            0xac, 0xf9, 0xd8, 0xe1, 0x32, 0x82, 0xf0, 0x63, 0xb7, 0xb3, 0x0d, 0x73, 0xd4, 0xd3,
            0x2c, 0x9a, 0xad, 0x6d, 0xfa, 0x8b, 0x27,
        ];
        let ec2_key = COSEEC2Key {
            curve: Curve::SECP256R1,
            x: SAMPLE_P256_X.to_vec(),
            y: SAMPLE_P256_Y.to_vec(),
        };
        let cose_key = COSEKey {
            alg: COSEAlgorithm::EDDSA,
            key: COSEKeyType::EC2(ec2_key),
        };
        let actual = cose_key.der_spki().expect("Failed to serialize key");
        assert_eq!(actual, expected);
    }

    #[test]
    fn test_ec2_key_to_cbor() {
        let ec2_key = COSEEC2Key {
            curve: Curve::SECP256R1,
            x: SAMPLE_P256_X.to_vec(),
            y: SAMPLE_P256_Y.to_vec(),
        };
        let cose_key = COSEKey {
            alg: COSEAlgorithm::EDDSA,
            key: COSEKeyType::EC2(ec2_key),
        };
        let cose_key_cbor = serde_cbor::to_vec(&cose_key).expect("Failed to serialize key");
        let actual = serde_cbor::from_slice(&cose_key_cbor).expect("Failed to deserialize key");
        assert_eq!(cose_key, actual);
    }

    #[test]
    fn test_okp_key_to_der_spki() {
        // RFC 8410 Example 10.1
        let expected = [
            0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00, 0x19, 0xbf,
            0x44, 0x09, 0x69, 0x84, 0xcd, 0xfe, 0x85, 0x41, 0xba, 0xc1, 0x67, 0xdc, 0x3b, 0x96,
            0xc8, 0x50, 0x86, 0xaa, 0x30, 0xb6, 0xb6, 0xcb, 0x0c, 0x5c, 0x38, 0xad, 0x70, 0x31,
            0x66, 0xe1,
        ];
        let okp_key = COSEOKPKey {
            curve: Curve::Ed25519,
            x: SAMPLE_ED25519_KEY.to_vec(),
        };
        let cose_key = COSEKey {
            alg: COSEAlgorithm::EDDSA,
            key: COSEKeyType::OKP(okp_key),
        };
        let actual = cose_key.der_spki().expect("Failed to serialize key");
        assert_eq!(actual, expected);
    }

    #[test]
    fn test_okp_key_to_cbor() {
        let okp_key = COSEOKPKey {
            curve: Curve::Ed25519,
            x: SAMPLE_ED25519_KEY.to_vec(),
        };
        let cose_key = COSEKey {
            alg: COSEAlgorithm::EDDSA,
            key: COSEKeyType::OKP(okp_key),
        };
        let cose_key_cbor = serde_cbor::to_vec(&cose_key).expect("Failed to serialize key");
        let actual = serde_cbor::from_slice(&cose_key_cbor).expect("Failed to deserialize key");
        assert_eq!(cose_key, actual);
    }

    #[test]
    fn test_parse_es256_serialize_key() {
        // Test values taken from https://github.com/Yubico/python-fido2/blob/master/test/test_cose.py
        let key_data = decode_hex("A5010203262001215820A5FD5CE1B1C458C530A54FA61B31BF6B04BE8B97AFDE54DD8CBB69275A8A1BE1225820FA3A3231DD9DEED9D1897BE5A6228C59501E4BCD12975D3DFF730F01278EA61C");
        let key: COSEKey = from_slice(&key_data).unwrap();
        assert_eq!(key.alg, COSEAlgorithm::ES256);
        if let COSEKeyType::EC2(ec2key) = &key.key {
            assert_eq!(ec2key.curve, Curve::SECP256R1);
            assert_eq!(
                ec2key.x,
                decode_hex("A5FD5CE1B1C458C530A54FA61B31BF6B04BE8B97AFDE54DD8CBB69275A8A1BE1")
            );
            assert_eq!(
                ec2key.y,
                decode_hex("FA3A3231DD9DEED9D1897BE5A6228C59501E4BCD12975D3DFF730F01278EA61C")
            );
        } else {
            panic!("Wrong key type!");
        }

        let serialized = serde_cbor::to_vec(&key).expect("Failed to serialize key");
        assert_eq!(key_data, serialized);
    }

    #[test]
    #[allow(non_snake_case)]
    fn test_shared_secret() {
        // Test values taken from https://github.com/Yubico/python-fido2/blob/main/tests/test_ctap2.py
        let EC_PRIV =
            decode_hex("7452E599FEE739D8A653F6A507343D12D382249108A651402520B72F24FE7684");
        let EC_PUB_X =
            decode_hex("44D78D7989B97E62EA993496C9EF6E8FD58B8B00715F9A89153DDD9C4657E47F");
        let EC_PUB_Y =
            decode_hex("EC802EE7D22BD4E100F12E48537EB4E7E96ED3A47A0A3BD5F5EEAB65001664F9");
        let DEV_PUB_X =
            decode_hex("0501D5BC78DA9252560A26CB08FCC60CBE0B6D3B8E1D1FCEE514FAC0AF675168");
        let DEV_PUB_Y =
            decode_hex("D551B3ED46F665731F95B4532939C25D91DB7EB844BD96D4ABD4083785F8DF47");
        let SHARED = decode_hex("c42a039d548100dfba521e487debcbbb8b66bb7496f8b1862a7a395ed83e1a1c");
        let TOKEN_ENC = decode_hex("7A9F98E31B77BE90F9C64D12E9635040");
        let TOKEN = decode_hex("aff12c6dcfbf9df52f7a09211e8865cd");
        let PIN_HASH_ENC = decode_hex("afe8327ce416da8ee3d057589c2ce1a9");

        let client_ec2_key = COSEEC2Key {
            curve: Curve::SECP256R1,
            x: EC_PUB_X.clone(),
            y: EC_PUB_Y.clone(),
        };

        let peer_ec2_key = COSEEC2Key {
            curve: Curve::SECP256R1,
            x: DEV_PUB_X,
            y: DEV_PUB_Y,
        };

        // We are using `test_cose_ec2_p256_ecdh_sha256()` here, because we need a way to hand in
        // the private key which would be generated on the fly otherwise (ephemeral keys),
        // to predict the outputs
        let shared_point =
            test_ecdh_p256_raw(&peer_ec2_key, &EC_PUB_X, &EC_PUB_Y, &EC_PRIV).unwrap();
        let shared_secret = SharedSecret {
            pin_protocol: PinUvAuthProtocol(Box::new(PinUvAuth1 {})),
            key: sha256(&shared_point).unwrap(),
            inputs: PublicInputs {
                client: COSEKey {
                    alg: COSEAlgorithm::ES256,
                    key: COSEKeyType::EC2(client_ec2_key),
                },
                peer: COSEKey {
                    alg: COSEAlgorithm::ES256,
                    key: COSEKeyType::EC2(peer_ec2_key),
                },
            },
        };
        assert_eq!(shared_secret.key, SHARED);

        let token_enc = shared_secret.encrypt(&TOKEN).unwrap();
        assert_eq!(token_enc, TOKEN_ENC);

        let token = shared_secret.decrypt(&TOKEN_ENC).unwrap();
        assert_eq!(token, TOKEN);

        let pin = Pin::new("1234");
        let pin_hash_enc = shared_secret.encrypt(&pin.for_pin_token()).unwrap();
        assert_eq!(pin_hash_enc, PIN_HASH_ENC);
    }

    #[test]
    fn test_pin_uv_auth2_kdf() {
        // We don't pull a complete HKDF implementation from the crypto backend, so we need to
        // check that PinUvAuth2::kdf makes the right sequence of HMAC-SHA256 calls.
        //
        // ```python
        // from cryptography.hazmat.primitives.kdf.hkdf import HKDF
        // from cryptography.hazmat.primitives import hashes
        // from cryptography.hazmat.backends import default_backend
        //
        // Z = b"\xFF" * 32
        //
        // hmac_key = HKDF(
        //     algorithm=hashes.SHA256(),
        //     length=32,
        //     salt=b"\x00" * 32,
        //     info=b"CTAP2 HMAC key",
        // ).derive(Z)
        //
        // aes_key = HKDF(
        //     algorithm=hashes.SHA256(),
        //     length=32,
        //     salt=b"\x00" * 32,
        //     info=b"CTAP2 AES key",
        // ).derive(Z)
        //
        // print((hmac_key+aes_key).hex())
        // ```
        let input = decode_hex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        let expected = decode_hex("570B4ED82AA5DFB49DB79DBEAF4B315D8ABB1A9867B245F3367026987C0D47A17D9A93C39BAEC741D141C6238D8E1846DE323D8EED022CB397D19A73B98945E2");
        let output = PinUvAuth2 {}.kdf(&input).unwrap();
        assert_eq!(&expected, &output);
    }

    #[test]
    fn test_hmac_sha256() {
        let key = "key";
        let message = "The quick brown fox jumps over the lazy dog";
        let expected =
            decode_hex("f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");

        let result = hmac_sha256(key.as_bytes(), message.as_bytes()).expect("HMAC-SHA256 failed");
        assert_eq!(result, expected);

        let key = "The quick brown fox jumps over the lazy dogThe quick brown fox jumps over the lazy dog";
        let message = "message";
        let expected =
            decode_hex("5597b93a2843078cbb0c920ae41dfe20f1685e10c67e423c11ab91adfc319d12");

        let result = hmac_sha256(key.as_bytes(), message.as_bytes()).expect("HMAC-SHA256 failed");
        assert_eq!(result, expected);
    }

    #[test]
    fn test_pin_encryption_and_hashing() {
        let pin = "1234";

        let shared_secret = vec![
            0x82, 0xE3, 0xD8, 0x41, 0xE2, 0x5C, 0x5C, 0x13, 0x46, 0x2C, 0x12, 0x3C, 0xC3, 0xD3,
            0x98, 0x78, 0x65, 0xBA, 0x3D, 0x20, 0x46, 0x74, 0xFB, 0xED, 0xD4, 0x7E, 0xF5, 0xAB,
            0xAB, 0x8D, 0x13, 0x72,
        ];
        let expected_new_pin_enc = vec![
            0x70, 0x66, 0x4B, 0xB5, 0x81, 0xE2, 0x57, 0x45, 0x1A, 0x3A, 0xB9, 0x1B, 0xF1, 0xAA,
            0xD8, 0xE4, 0x5F, 0x6C, 0xE9, 0xB5, 0xC3, 0xB0, 0xF3, 0x2B, 0x5E, 0xCD, 0x62, 0xD0,
            0xBA, 0x3B, 0x60, 0x5F, 0xD9, 0x18, 0x31, 0x66, 0xF6, 0xC5, 0xFA, 0xF3, 0xE4, 0xDA,
            0x24, 0x81, 0x50, 0x2C, 0xD0, 0xCE, 0xE0, 0x15, 0x8B, 0x35, 0x1F, 0xC3, 0x92, 0x08,
            0xA7, 0x7C, 0xB2, 0x74, 0x4B, 0xD4, 0x3C, 0xF9,
        ];
        let expected_pin_auth = vec![
            0x8E, 0x7F, 0x01, 0x69, 0x97, 0xF3, 0xB0, 0xA2, 0x7B, 0xA4, 0x34, 0x7A, 0x0E, 0x49,
            0xFD, 0xF5,
        ];

        let mut input = vec![0x00; 64];
        {
            let pin_bytes = pin.as_bytes();
            let (head, _) = input.split_at_mut(pin_bytes.len());
            head.copy_from_slice(pin_bytes);
        }

        let new_pin_enc = PinUvAuth1 {}
            .encrypt(&shared_secret, &input)
            .expect("Failed to encrypt pin");
        assert_eq!(new_pin_enc, expected_new_pin_enc);

        let pin_auth = PinUvAuth1 {}
            .authenticate(&shared_secret, &new_pin_enc)
            .expect("HMAC-SHA256 failed");
        assert_eq!(pin_auth[0..16], expected_pin_auth);
    }

    #[test]
    fn test_pin_protocol() {
        let mut info = AuthenticatorInfo {
            versions: vec![AuthenticatorVersion::U2F_V2, AuthenticatorVersion::FIDO_2_0],
            extensions: vec![],
            aaguid: AAGuid(AAGUID_RAW),
            options: AuthenticatorOptions {
                platform_device: false,
                resident_key: true,
                client_pin: Some(false),
                user_presence: true,
                ..Default::default()
            },
            max_msg_size: Some(1200),
            pin_protocols: None,
            ..Default::default()
        };

        // Valid pin_protocols
        info.pin_protocols = Some(vec![1, 2]);
        let pin = PinUvAuthProtocol::try_from(&info).unwrap();
        assert_eq!(pin.id(), 1); // The one listed first

        // Invalid pin_protocols
        info.pin_protocols = Some(vec![0, 10]);
        PinUvAuthProtocol::try_from(&info).unwrap_err();

        info.pin_protocols = None;
        // No PIN protocols. CTAP1 - not supported
        info.versions = vec![AuthenticatorVersion::U2F_V2];
        PinUvAuthProtocol::try_from(&info).unwrap_err();

        // No PIN protocols. CTAP2.0 - Fallback to 1
        info.versions = vec![AuthenticatorVersion::U2F_V2, AuthenticatorVersion::FIDO_2_0];
        let pin = PinUvAuthProtocol::try_from(&info).unwrap();
        assert_eq!(pin.id(), 1);

        // No PIN protocols. CTAP2.1 - Fallback to 2
        info.versions = vec![AuthenticatorVersion::FIDO_2_1];
        let pin = PinUvAuthProtocol::try_from(&info).unwrap();
        assert_eq!(pin.id(), 2);

        // No PIN protocols. CTAP2.1_PRE - Fallback to 2
        info.versions = vec![
            AuthenticatorVersion::FIDO_2_0,
            AuthenticatorVersion::FIDO_2_1_PRE,
        ];
        let pin = PinUvAuthProtocol::try_from(&info).unwrap();
        assert_eq!(pin.id(), 2);
    }

    #[test]
    fn test_sign() {
        let (good_private, good_public) =
            COSEKey::generate(COSEAlgorithm::ES256).expect("could not generate a key pair");
        let good_spki = match good_public.key {
            COSEKeyType::EC2(ref x) => x.der_spki().expect("could not serialize public key"),
            _ => unreachable!(),
        };

        let good_data = vec![1, 2, 3, 4, 5, 6, 7, 8];
        let good_signature =
            ecdsa_p256_sha256_sign_raw(&good_private, &good_data).expect("could not sign");
        let good_signature2 =
            ecdsa_p256_sha256_sign_raw(&good_private, &good_data).expect("could not sign");

        // Signing is randomized
        assert_ne!(good_signature, good_signature2);

        // Good signature verifies
        assert!(test_ecdsa_p256_sha256_verify_raw(&good_spki, &good_signature, &good_data).is_ok());

        // Wrong data does not verify
        let other_data = vec![0, 0, 0, 0, 5, 6, 7, 8];
        assert!(
            test_ecdsa_p256_sha256_verify_raw(&good_spki, &good_signature, &other_data).is_err()
        );

        // Wrong signature does not verify
        let other_signature =
            ecdsa_p256_sha256_sign_raw(&good_private, &other_data).expect("could not sign");
        assert!(
            test_ecdsa_p256_sha256_verify_raw(&good_spki, &other_signature, &good_data).is_err()
        );

        // Wrong key does not verify
        let (_, other_public) =
            COSEKey::generate(COSEAlgorithm::ES256).expect("could not generate a key pair");
        let other_spki = match other_public.key {
            COSEKeyType::EC2(ref x) => x.der_spki().expect("could not serialize public key"),
            _ => unreachable!(),
        };
        assert!(
            test_ecdsa_p256_sha256_verify_raw(&other_spki, &good_signature, &good_data).is_err()
        );
    }
}
