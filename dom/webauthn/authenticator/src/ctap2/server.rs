/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use super::commands::get_assertion::HmacSecretExtension;
use crate::crypto::{COSEAlgorithm, CryptoError, PinUvAuthToken, SharedSecret};
use crate::{errors::AuthenticatorError, AuthenticatorTransports, KeyHandle};
use base64::Engine;
use icu_segmenter::{GraphemeClusterSegmenter, GraphemeClusterSegmenterBorrowed};
use serde::de::MapAccess;
use serde::{
    de::{Error as SerdeError, Unexpected, Visitor},
    Deserialize, Deserializer, Serialize, Serializer,
};
use serde_bytes::{ByteBuf, Bytes};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::convert::{Into, TryFrom};
use std::fmt;
use std::str::FromStr;

#[derive(Serialize, Deserialize, PartialEq, Eq, Clone)]
pub struct RpIdHash(pub [u8; 32]);

impl fmt::Debug for RpIdHash {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let value = base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(self.0);
        write!(f, "RpIdHash({value})")
    }
}

impl AsRef<[u8]> for RpIdHash {
    fn as_ref(&self) -> &[u8] {
        self.0.as_ref()
    }
}

impl RpIdHash {
    pub fn from(src: &[u8]) -> Result<RpIdHash, AuthenticatorError> {
        let mut payload = [0u8; 32];
        if src.len() != payload.len() {
            Err(AuthenticatorError::InvalidRelyingPartyInput)
        } else {
            payload.copy_from_slice(src);
            Ok(RpIdHash(payload))
        }
    }
}

// NOTE: WebAuthn requires all fields and CTAP2 does not.
#[derive(Debug, Serialize, Clone, Default, Deserialize, PartialEq, Eq)]
pub struct RelyingParty {
    pub id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
}

impl RelyingParty {
    pub fn from<S>(id: S) -> Self
    where
        S: Into<String>,
    {
        Self {
            id: id.into(),
            name: None,
        }
    }

    pub fn hash(&self) -> RpIdHash {
        RpIdHash(Sha256::digest(&self.id).into())
    }
}

// NOTE: WebAuthn requires all fields and CTAP2 does not.
#[derive(Debug, Clone, Eq, PartialEq, Deserialize, Default)]
pub struct PublicKeyCredentialUserEntity {
    #[serde(with = "serde_bytes")]
    pub id: Vec<u8>,
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none", rename = "displayName")]
    pub display_name: Option<String>,
}

/// Truncate a user name or user display name to the start of a grapheme cluster.
/// <https://w3c.github.io/webauthn/#sctn-strings-truncation>
fn truncate_username(username: &str) -> Option<&str> {
    const MAX_LENGTH: usize = 64;
    const SEGMENTER: GraphemeClusterSegmenterBorrowed<'static> = GraphemeClusterSegmenter::new();

    if username.len() < MAX_LENGTH {
        return Some(username);
    }

    let mut len = 0;
    for segment in SEGMENTER.segment_str(username) {
        if segment == MAX_LENGTH {
            return Some(&username[..segment]);
        } else if segment > MAX_LENGTH {
            return Some(&username[..len]);
        } else {
            len = segment;
        }
    }

    // If we got here, then we have no segments.
    None
}

impl Serialize for PublicKeyCredentialUserEntity {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        debug!("Serialize PublicKeyCredentialUserEntity");
        let name = self.name.as_ref().and_then(|u| truncate_username(u));
        let display_name = self
            .display_name
            .as_ref()
            .and_then(|u| truncate_username(u));

        serialize_map_optional!(
            serializer,
            &"id" => Some(Bytes::new(&self.id)),
            &"name" => name,
            &"displayName" => display_name,
        )
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PublicKeyCredentialParameters {
    pub alg: COSEAlgorithm,
}

impl TryFrom<i32> for PublicKeyCredentialParameters {
    type Error = AuthenticatorError;
    fn try_from(arg: i32) -> Result<Self, Self::Error> {
        let alg = COSEAlgorithm::try_from(arg as i64)?;
        Ok(PublicKeyCredentialParameters { alg })
    }
}

impl Serialize for PublicKeyCredentialParameters {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serialize_map!(
            serializer,
            "alg" => &self.alg,
            "type" => "public-key",
        )
    }
}

impl<'de> Deserialize<'de> for PublicKeyCredentialParameters {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct PublicKeyCredentialParametersVisitor;

        impl<'de> Visitor<'de> for PublicKeyCredentialParametersVisitor {
            type Value = PublicKeyCredentialParameters;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a map")
            }

            fn visit_map<M>(self, mut map: M) -> Result<Self::Value, M::Error>
            where
                M: MapAccess<'de>,
            {
                let mut found_type = false;
                let mut alg = None;
                while let Some(key) = map.next_key()? {
                    match key {
                        "alg" => {
                            if alg.is_some() {
                                return Err(SerdeError::duplicate_field("alg"));
                            }
                            alg = Some(map.next_value()?);
                        }
                        "type" => {
                            if found_type {
                                return Err(SerdeError::duplicate_field("type"));
                            }

                            let v: &str = map.next_value()?;
                            if v != "public-key" {
                                return Err(SerdeError::custom(format!("invalid value: {v}")));
                            }
                            found_type = true;
                        }
                        v => {
                            return Err(SerdeError::unknown_field(v, &[]));
                        }
                    }
                }

                if !found_type {
                    return Err(SerdeError::missing_field("type"));
                }

                let alg = alg.ok_or_else(|| SerdeError::missing_field("alg"))?;

                Ok(PublicKeyCredentialParameters { alg })
            }
        }

        deserializer.deserialize_bytes(PublicKeyCredentialParametersVisitor)
    }
}

#[derive(Debug, PartialEq, Serialize, Deserialize, Eq, Clone)]
#[serde(rename_all = "lowercase")]
pub enum Transport {
    USB,
    NFC,
    BLE,
    Internal,
}

impl From<AuthenticatorTransports> for Vec<Transport> {
    fn from(t: AuthenticatorTransports) -> Self {
        let mut transports = Vec::new();
        if t.contains(AuthenticatorTransports::USB) {
            transports.push(Transport::USB);
        }
        if t.contains(AuthenticatorTransports::NFC) {
            transports.push(Transport::NFC);
        }
        if t.contains(AuthenticatorTransports::BLE) {
            transports.push(Transport::BLE);
        }

        transports
    }
}

pub type PublicKeyCredentialId = Vec<u8>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PublicKeyCredentialDescriptor {
    pub id: PublicKeyCredentialId,
    pub transports: Vec<Transport>,
}

impl Serialize for PublicKeyCredentialDescriptor {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serialize_map!(
            serializer,
            "id" => Bytes::new(&self.id),
            "type" => "public-key",

            // TODO(MS): Transports is OPTIONAL, but some older tokens don't understand it
            //           and return a CBOR-Parsing error. It is only a hint for the token,
            //           so we'll leave it out for the moment
            // "transports" => &self.transports,
        )
    }
}

impl<'de> Deserialize<'de> for PublicKeyCredentialDescriptor {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct PublicKeyCredentialDescriptorVisitor;

        impl<'de> Visitor<'de> for PublicKeyCredentialDescriptorVisitor {
            type Value = PublicKeyCredentialDescriptor;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a map")
            }

            fn visit_map<M>(self, mut map: M) -> Result<Self::Value, M::Error>
            where
                M: MapAccess<'de>,
            {
                let mut found_type = false;
                let mut id = None;
                let mut transports = None;
                while let Some(key) = map.next_key()? {
                    match key {
                        "id" => {
                            if id.is_some() {
                                return Err(SerdeError::duplicate_field("id"));
                            }
                            let id_bytes: ByteBuf = map.next_value()?;
                            id = Some(id_bytes.into_vec());
                        }
                        "transports" => {
                            if transports.is_some() {
                                return Err(SerdeError::duplicate_field("transports"));
                            }
                            transports = Some(map.next_value()?);
                        }
                        "type" => {
                            if found_type {
                                return Err(SerdeError::duplicate_field("type"));
                            }
                            let v: &str = map.next_value()?;
                            if v != "public-key" {
                                return Err(SerdeError::custom(format!("invalid value: {v}")));
                            }
                            found_type = true;
                        }
                        v => {
                            return Err(SerdeError::unknown_field(v, &[]));
                        }
                    }
                }

                if !found_type {
                    return Err(SerdeError::missing_field("type"));
                }

                let id = id.ok_or_else(|| SerdeError::missing_field("id"))?;
                let transports = transports.unwrap_or_default();

                Ok(PublicKeyCredentialDescriptor { id, transports })
            }
        }

        deserializer.deserialize_any(PublicKeyCredentialDescriptorVisitor)
    }
}

impl From<&KeyHandle> for PublicKeyCredentialDescriptor {
    fn from(kh: &KeyHandle) -> Self {
        Self {
            id: kh.credential.clone(),
            transports: kh.transports.into(),
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Serialize)]
pub enum ResidentKeyRequirement {
    Discouraged,
    Preferred,
    Required,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Serialize)]
pub enum UserVerificationRequirement {
    Discouraged,
    Preferred,
    Required,
}

impl FromStr for UserVerificationRequirement {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        // https://www.w3.org/TR/webauthn-3/#enumdef-userverificationrequirement
        match s {
            "required" => Ok(Self::Required),
            "preferred" => Ok(Self::Preferred),
            "discouraged" => Ok(Self::Discouraged),
            s => Err(s.to_string()),
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum CredentialProtectionPolicy {
    UserVerificationOptional = 1,
    UserVerificationOptionalWithCredentialIDList = 2,
    UserVerificationRequired = 3,
}

impl Serialize for CredentialProtectionPolicy {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_u64(*self as u64)
    }
}

impl<'de> Deserialize<'de> for CredentialProtectionPolicy {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct CredentialProtectionPolicyVisitor;

        impl<'de> Visitor<'de> for CredentialProtectionPolicyVisitor {
            type Value = CredentialProtectionPolicy;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("an integer")
            }

            fn visit_u64<E>(self, v: u64) -> Result<Self::Value, E>
            where
                E: SerdeError,
            {
                match v {
                    1 => Ok(CredentialProtectionPolicy::UserVerificationOptional),
                    2 => Ok(
                        CredentialProtectionPolicy::UserVerificationOptionalWithCredentialIDList,
                    ),
                    3 => Ok(CredentialProtectionPolicy::UserVerificationRequired),
                    _ => Err(SerdeError::invalid_value(
                        Unexpected::Unsigned(v),
                        &"valid CredentialProtectionPolicy",
                    )),
                }
            }
        }

        deserializer.deserialize_any(CredentialProtectionPolicyVisitor)
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum AuthenticatorExtensionsCredBlob {
    /// Used in GetAssertion-requests to request the stored blob,
    /// and in MakeCredential-responses to signify if the
    /// storing worked.
    AsBool(bool),
    /// Used in MakeCredential-requests to store a new credBlob,
    /// and in GetAssertion-responses when retrieving the
    /// stored blob.
    #[serde(serialize_with = "vec_to_bytebuf", deserialize_with = "bytebuf_to_vec")]
    AsBytes(Vec<u8>),
}

fn vec_to_bytebuf<S>(data: &[u8], s: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    ByteBuf::from(data).serialize(s)
}

fn bytebuf_to_vec<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: Deserializer<'de>,
{
    let bytes = <ByteBuf>::deserialize(deserializer)?;
    Ok(bytes.to_vec())
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct AuthenticationExtensionsClientInputs {
    pub app_id: Option<String>,
    pub cred_props: Option<bool>,
    pub credential_protection_policy: Option<CredentialProtectionPolicy>,
    pub enforce_credential_protection_policy: Option<bool>,
    pub hmac_create_secret: Option<bool>,
    pub hmac_get_secret: Option<HMACGetSecretInput>,
    pub min_pin_length: Option<bool>,
    pub prf: Option<AuthenticationExtensionsPRFInputs>,
    /// MakeCredential-requests use AsBytes
    /// GetAssertion-requests use AsBool
    pub cred_blob: Option<AuthenticatorExtensionsCredBlob>,
    pub large_blob_key: Option<bool>,
    // CTAP 2.2
    pub third_party_payment: Option<bool>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct CredentialProperties {
    pub rk: bool,
}

/// Salt inputs for the `hmac-secret` extension.
/// https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html#dictdef-hmacgetsecretinput
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize)]
pub struct HMACGetSecretInput {
    pub salt1: [u8; 32],
    pub salt2: Option<[u8; 32]>,
}

/// Decrypted HMAC outputs from the `hmac-secret` extension.
/// https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html#dictdef-hmacgetsecretoutput
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct HMACGetSecretOutput {
    pub output1: [u8; 32],
    pub output2: Option<[u8; 32]>,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct AuthenticationExtensionsPRFInputs {
    pub eval: Option<AuthenticationExtensionsPRFValues>,
    pub eval_by_credential: Option<HashMap<Vec<u8>, AuthenticationExtensionsPRFValues>>,
}

impl AuthenticationExtensionsPRFInputs {
    /// Select an `eval` or `evalByCredential` entry and calculate hmac-secret salt inputs from those inputs.
    ///
    /// Returns [None] if the `eval` input was not given and no credential in `allow_credentials` matched any `evalByCredential` entry.
    /// Otherwise returns the initialized [HmacSecretExtension] and, if an `evalByCredential` entry was used to compute the salt inputs,
    /// the [PublicKeyCredentialDescriptor] matching that `evalByCredential` entry.
    /// If present, `allowCredentials` SHOULD be set to contain only that [PublicKeyCredentialDescriptor] value.
    pub fn calculate<'allow_cred>(
        &self,
        secret: &SharedSecret,
        allow_credentials: &'allow_cred [PublicKeyCredentialDescriptor],
        puat: Option<&PinUvAuthToken>,
    ) -> Result<
        Option<(
            HmacSecretExtension,
            Option<&'allow_cred PublicKeyCredentialDescriptor>,
        )>,
        CryptoError,
    > {
        if let Some((selected_credential, ev)) = self.select_eval(allow_credentials) {
            let mut hmac_secret = HmacSecretExtension::new(
                Self::eval_to_salt(&ev.first).to_vec(),
                ev.second
                    .as_ref()
                    .map(|second| Self::eval_to_salt(second).to_vec()),
            );
            hmac_secret.calculate(secret, puat)?;
            Ok(Some((hmac_secret, selected_credential)))
        } else {
            Ok(None)
        }
    }

    /// Select an `evalByCredential` entry matching any element of `allow_credentials`,
    /// or otherwise fall back to `eval`, if present, if no match is found.
    fn select_eval<'allow_cred>(
        &self,
        allow_credentials: &'allow_cred [PublicKeyCredentialDescriptor],
    ) -> Option<(
        Option<&'allow_cred PublicKeyCredentialDescriptor>,
        &AuthenticationExtensionsPRFValues,
    )> {
        self.select_credential(allow_credentials)
            .map(|(cred, ev)| (Some(cred), ev))
            .or(self.eval.as_ref().map(|eval| (None, eval)))
    }

    /// Select an `evalByCredential` entry matching any element of `allow_credentials`.
    fn select_credential<'allow_cred>(
        &self,
        allow_credentials: &'allow_cred [PublicKeyCredentialDescriptor],
    ) -> Option<(
        &'allow_cred PublicKeyCredentialDescriptor,
        &AuthenticationExtensionsPRFValues,
    )> {
        self.eval_by_credential
            .as_ref()
            .and_then(|eval_by_credential| {
                allow_credentials
                    .iter()
                    .find_map(|pkcd| eval_by_credential.get(&pkcd.id).map(|eval| (pkcd, eval)))
            })
    }

    /// Convert a PRF eval input to an hmac-secret salt input.
    fn eval_to_salt(eval: &[u8]) -> [u8; 32] {
        Sha256::new_with_prefix(b"WebAuthn PRF")
            .chain_update([0x00].iter())
            .chain_update(eval.iter())
            .finalize()
            .into()
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize)]
pub struct AuthenticationExtensionsPRFValues {
    pub first: Vec<u8>,
    pub second: Option<Vec<u8>>,
}

impl From<HMACGetSecretOutput> for AuthenticationExtensionsPRFValues {
    fn from(hmac_output: HMACGetSecretOutput) -> Self {
        Self {
            first: hmac_output.output1.to_vec(),
            second: hmac_output.output2.map(|o2| o2.to_vec()),
        }
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct AuthenticationExtensionsPRFOutputs {
    pub enabled: Option<bool>,
    pub results: Option<AuthenticationExtensionsPRFValues>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct AuthenticationExtensionsClientOutputs {
    pub app_id: Option<bool>,
    pub cred_props: Option<CredentialProperties>,
    pub hmac_create_secret: Option<bool>,
    pub hmac_get_secret: Option<HMACGetSecretOutput>,
    pub prf: Option<AuthenticationExtensionsPRFOutputs>,
    /// MakeCredential-responses use AsBool
    /// GetAssertion-responses use AsBytes
    pub cred_blob: Option<AuthenticatorExtensionsCredBlob>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AuthenticatorAttachment {
    CrossPlatform,
    Platform,
    Unknown,
}

#[cfg(test)]
mod test {
    use super::{
        truncate_username, COSEAlgorithm, PublicKeyCredentialDescriptor,
        PublicKeyCredentialParameters, PublicKeyCredentialUserEntity, RelyingParty, Transport,
    };
    use serde_cbor::from_slice;

    fn create_user() -> PublicKeyCredentialUserEntity {
        PublicKeyCredentialUserEntity {
            id: vec![
                0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x30,
                0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x30, 0x82,
                0x01, 0x93, 0x30, 0x82,
            ],
            name: Some(String::from("johnpsmith@example.com")),
            display_name: Some(String::from("John P. Smith")),
        }
    }
    #[test]
    fn serialize_rp() {
        let rp = RelyingParty {
            id: String::from("Acme"),
            name: None,
        };

        let payload = ser::to_vec(&rp).unwrap();
        assert_eq!(
            &payload,
            &[
                0xa1, // map(1)
                0x62, // text(2)
                0x69, 0x64, // "id"
                0x64, // text(4)
                0x41, 0x63, 0x6d, 0x65
            ]
        );
    }

    #[test]
    fn test_deserialize_user() {
        // This includes an obsolete "icon" field to test that deserialization
        // ignores it.
        let input = vec![
            0xa4, // map(4)
            0x62, // text(2)
            0x69, 0x64, // "id"
            0x58, 0x20, // bytes(32)
            0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, // userid
            0x02, 0x01, 0x02, 0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, // ...
            0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x30, 0x82, 0x01, 0x93, // ...
            0x30, 0x82, // ...
            0x64, // text(4)
            0x69, 0x63, 0x6f, 0x6e, // "icon"
            0x78, 0x2b, // text(43)
            0x68, 0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x70,
            0x69, // "https://pics.example.com/00/p/aBjjjpqPb.png"
            0x63, 0x73, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, // ...
            0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x30, 0x30, 0x2f, 0x70, 0x2f, // ...
            0x61, 0x42, 0x6a, 0x6a, 0x6a, 0x70, 0x71, 0x50, 0x62, 0x2e, // ...
            0x70, 0x6e, 0x67, // ...
            0x64, // text(4)
            0x6e, 0x61, 0x6d, 0x65, // "name"
            0x76, // text(22)
            0x6a, 0x6f, 0x68, 0x6e, 0x70, 0x73, 0x6d, 0x69, 0x74,
            0x68, // "johnpsmith@example.com"
            0x40, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, // ...
            0x6f, 0x6d, // ...
            0x6b, // text(11)
            0x64, 0x69, 0x73, 0x70, 0x6c, 0x61, 0x79, 0x4e, 0x61, 0x6d, // "displayName"
            0x65, // ...
            0x6d, // text(13)
            0x4a, 0x6f, 0x68, 0x6e, 0x20, 0x50, 0x2e, 0x20, 0x53, 0x6d, // "John P. Smith"
            0x69, 0x74, 0x68, // ...
        ];
        let expected = create_user();
        let actual: PublicKeyCredentialUserEntity = from_slice(&input).unwrap();
        assert_eq!(expected, actual);
    }

    #[test]
    fn serialize_user() {
        let user = create_user();

        let payload = ser::to_vec(&user).unwrap();
        println!("payload = {payload:?}");
        assert_eq!(
            payload,
            vec![
                0xa3, // map(3)
                0x62, // text(2)
                0x69, 0x64, // "id"
                0x58, 0x20, // bytes(32)
                0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, // userid
                0x02, 0x01, 0x02, 0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, // ...
                0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x30, 0x82, 0x01, 0x93, // ...
                0x30, 0x82, // ...
                0x64, // text(4)
                0x6e, 0x61, 0x6d, 0x65, // "name"
                0x76, // text(22)
                0x6a, 0x6f, 0x68, 0x6e, 0x70, 0x73, 0x6d, 0x69, 0x74,
                0x68, // "johnpsmith@example.com"
                0x40, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, // ...
                0x6f, 0x6d, // ...
                0x6b, // text(11)
                0x64, 0x69, 0x73, 0x70, 0x6c, 0x61, 0x79, 0x4e, 0x61, 0x6d, // "displayName"
                0x65, // ...
                0x6d, // text(13)
                0x4a, 0x6f, 0x68, 0x6e, 0x20, 0x50, 0x2e, 0x20, 0x53, 0x6d, // "John P. Smith"
                0x69, 0x74, 0x68, // ...
            ]
        );
    }

    #[test]
    fn serialize_user_nodisplayname() {
        let user = PublicKeyCredentialUserEntity {
            id: vec![
                0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x30,
                0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x30, 0x82,
                0x01, 0x93, 0x30, 0x82,
            ],
            name: Some(String::from("johnpsmith@example.com")),
            display_name: None,
        };

        let payload = ser::to_vec(&user).unwrap();
        println!("payload = {payload:?}");
        assert_eq!(
            payload,
            vec![
                0xa2, // map(2)
                0x62, // text(2)
                0x69, 0x64, // "id"
                0x58, 0x20, // bytes(32)
                0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, // userid
                0x02, 0x01, 0x02, 0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, // ...
                0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x30, 0x82, 0x01, 0x93, // ...
                0x30, 0x82, // ...
                0x64, // text(4)
                0x6e, 0x61, 0x6d, 0x65, // "name"
                0x76, // text(22)
                0x6a, 0x6f, 0x68, 0x6e, 0x70, 0x73, 0x6d, 0x69, 0x74,
                0x68, // "johnpsmith@example.com"
                0x40, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, // ...
                0x6f, 0x6d, // ...
            ]
        );
    }

    use serde_cbor::ser;

    #[test]
    fn public_key() {
        let keys = vec![
            PublicKeyCredentialParameters {
                alg: COSEAlgorithm::ES256,
            },
            PublicKeyCredentialParameters {
                alg: COSEAlgorithm::RS256,
            },
        ];

        let payload = ser::to_vec(&keys);
        println!("payload = {payload:?}");
        let payload = payload.unwrap();
        assert_eq!(
            payload,
            vec![
                0x82, // array(2)
                0xa2, // map(2)
                0x63, // text(3)
                0x61, 0x6c, 0x67, // "alg"
                0x26, // -7 (ES256)
                0x64, // text(4)
                0x74, 0x79, 0x70, 0x65, // "type"
                0x6a, // text(10)
                0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, // "public-key"
                0x2D, 0x6B, 0x65, 0x79, // ...
                0xa2, // map(2)
                0x63, // text(3)
                0x61, 0x6c, 0x67, // "alg"
                0x39, 0x01, 0x00, // -257 (RS256)
                0x64, // text(4)
                0x74, 0x79, 0x70, 0x65, // "type"
                0x6a, // text(10)
                0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, // "public-key"
                0x2D, 0x6B, 0x65, 0x79 // ...
            ]
        );
    }

    #[test]
    fn public_key_desc() {
        let key = PublicKeyCredentialDescriptor {
            id: vec![
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                0x1c, 0x1d, 0x1e, 0x1f,
            ],
            transports: vec![Transport::BLE, Transport::USB],
        };

        let payload = ser::to_vec(&key);
        println!("payload = {payload:?}");
        let payload = payload.unwrap();

        assert_eq!(
            payload,
            vec![
                // 0xa3, // map(3)
                0xa2, // map(2)
                0x62, // text(2)
                0x69, 0x64, // "id"
                0x58, 0x20, // bytes(32)
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, // key id
                0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, // ...
                0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, // ...
                0x12, 0x13, 0x14, 0x15, 0x16, 0x17, // ...
                0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, // ...
                0x1e, 0x1f, // ...
                0x64, // text(4)
                0x74, 0x79, 0x70, 0x65, // "type"
                0x6a, // text(10)
                0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, // "public-key"
                0x2D, 0x6B, 0x65,
                0x79, // ...

                      // Deactivated for now
                      //0x6a, // text(10)
                      //0x74, 0x72, 0x61, 0x6e, 0x73, 0x70, // "transports"
                      //0x6f, 0x72, 0x74, 0x73, // ...
                      //0x82, // array(2)
                      //0x63, // text(3)
                      //0x62, 0x6c, 0x65, // "ble"
                      //0x63, // text(3)
                      //0x75, 0x73, 0x62 // "usb"
            ]
        );
    }

    #[test]
    fn username_truncation() {
        for (input, expected) in [
            ("", Some("")),
            (
                "A username of exactly 64 bytes in length and not one more byte!!",
                Some("A username of exactly 64 bytes in length and not one more byte!!"),
            ),
            (
                "A username of more than sixty-four bytes in length, which should be truncated.",
                Some("A username of more than sixty-four bytes in length, which should"),
            ),
            // Truncating at 64 bytes would be valid UTF-8, but remove the combining character.
            (
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaag\u{308}",
                Some("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            ),
            // Truncating these at 64 bytes would result in invalid UTF-8.
            (
                // From https://ja.wikipedia.org/w/index.php?title=Mozilla_Firefox&oldid=111356400
                "単にFirefoxと呼称されるほか、fireおよびfoxの和訳から火狐とも呼称される。",
                Some("単にFirefoxと呼称されるほか、fireおよびfoxの和"),
            ),
            (
                // From https://ko.wikipedia.org/w/index.php?title=%ED%8C%8C%EC%9D%B4%EC%96%B4%ED%8F%AD%EC%8A%A4&oldid=42442268
                "모질라 파이어폭스(Mozilla Firefox)는 모질라 재단과 모질라 코퍼레이션이 개발하는 자유 소프트웨어 웹 브라우저로",
                Some("모질라 파이어폭스(Mozilla Firefox)는 모질라 재단"),
            ),
            // Emoji 13.0
            // 6 flags: each is 1 grapheme cluster, 5 codepoints, 16 bytes, and includes a ZWJ.
            (
                "\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}",
                Some("\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}"),
            ),
            // Truncating at 64 bytes would be valid UTF-8, but remove the variation selector that
            // was part of the original input.
            (
                "3  \u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}",
                Some("3  \u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}"),
            ),
            // Truncating at 64 bytes would be valid UTF-8, but leave a trailing ZWJ.
            (
                "Flags \u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}",
                Some("Flags \u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}"),
            ),
            // Truncating at 64 bytes would be valid UTF-8, but leave a trailing 🏳️.
            (
                "3  Flags \u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}",
                Some("3  Flags \u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}\u{1F3F3}\u{FE0F}\u{200D}\u{26A7}\u{FE0F}"),
            ),
            // Emoji 17.0
            // Each emoji is 1 grapheme cluster, 7 codepoints, 26 bytes, and includes multiple ZWJs.
            (
                "\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}",
                Some("\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}"),
            ),
            // Truncating at 64 bytes would be valid UTF-8, but leave a trailing ZWJ.
            (
                " \u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}",
                Some(" \u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}"),
            ),
            // Truncating at 64 bytes would be valid UTF-8, but leave a trailing emoji.
            (
                "4   \u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}",
                Some("4   \u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}\u{1F9D1}\u{1F3FB}\u{200D}\u{1F430}\u{200D}\u{1F9D1}\u{1F3FC}"),
            ),
        ] {
            assert_eq!(
                expected,
                truncate_username(input),
                "for input username: {input:?}"
            );
        }
    }
}
