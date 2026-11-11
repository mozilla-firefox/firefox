/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use super::get_info::{AuthenticatorInfo, AuthenticatorVersion};
use super::{
    Command, CommandError, CtapResponse, PinUvAuthCommand, RequestCtap1, RequestCtap2, Retryable,
    StatusCode,
};
use crate::consts::{PARAMETER_SIZE, U2F_REGISTER, U2F_REQUEST_USER_PRESENCE};
use crate::crypto::{
    parse_u2f_der_certificate, COSEAlgorithm, COSEEC2Key, COSEKey, COSEKeyType, Curve,
    PinUvAuthParam, PinUvAuthToken,
};
use crate::ctap2::attestation::{
    AAGuid, AttestationObject, AttestationStatement, AttestationStatementFidoU2F,
    AttestedCredentialData, AuthenticatorData, AuthenticatorDataFlags, HmacSecretResponse,
};
use crate::ctap2::client_data::ClientDataHash;
use crate::ctap2::server::{
    AuthenticationExtensionsClientInputs, AuthenticationExtensionsClientOutputs,
    AuthenticationExtensionsPRFOutputs, AuthenticatorAttachment, AuthenticatorExtensionsCredBlob,
    CredentialProtectionPolicy, PublicKeyCredentialDescriptor, PublicKeyCredentialParameters,
    PublicKeyCredentialUserEntity, RelyingParty, RpIdHash, UserVerificationRequirement,
};
use crate::ctap2::utils::{read_byte, serde_parse_err};
use crate::errors::AuthenticatorError;
use crate::transport::errors::{ApduErrorStatus, HIDError};
use crate::transport::{FidoDevice, VirtualFidoDevice};
use crate::u2ftypes::CTAP1RequestAPDU;
use serde::{
    de::{Error as DesError, MapAccess, Unexpected, Visitor},
    Deserialize, Deserializer, Serialize, Serializer,
};
use serde_bytes::ByteBuf;
use serde_cbor::{self, de::from_slice, ser, Value};
use std::collections::HashMap;
use std::fmt;
use std::io::{Cursor, Read};

#[derive(Debug, PartialEq, Eq)]
pub struct MakeCredentialsResult {
    pub att_obj: AttestationObject,
    pub attachment: AuthenticatorAttachment,
    pub extensions: AuthenticationExtensionsClientOutputs,
    pub ep_attestation: Option<bool>,
    pub large_blob_key: Option<Vec<u8>>,
    pub unsigned_extension_outputs: Option<HashMap<String, serde_cbor::Value>>,
}

impl MakeCredentialsResult {
    pub fn from_ctap1(input: &[u8], rp_id_hash: &RpIdHash) -> Result<Self, CommandError> {
        let mut data = Cursor::new(input);
        let magic_num = read_byte(&mut data).map_err(CommandError::Deserializing)?;
        if magic_num != 0x05 {
            error!("error while parsing registration: magic header not 0x05, but {magic_num}");
            return Err(CommandError::Deserializing(DesError::invalid_value(
                serde::de::Unexpected::Unsigned(magic_num as u64),
                &"0x05",
            )));
        }
        let mut public_key = [0u8; 65];
        data.read_exact(&mut public_key)
            .map_err(|_| CommandError::Deserializing(serde_parse_err("PublicKey")))?;

        let credential_id_len = read_byte(&mut data).map_err(CommandError::Deserializing)?;
        let mut credential_id = vec![0u8; credential_id_len as usize];
        data.read_exact(&mut credential_id)
            .map_err(|_| CommandError::Deserializing(serde_parse_err("CredentialId")))?;

        let cert_and_sig = parse_u2f_der_certificate(&data.get_ref()[data.position() as usize..])
            .map_err(|err| {
            CommandError::Deserializing(serde_parse_err(&format!(
                "Certificate and Signature: {err:?}",
            )))
        })?;

        let credential_ec2_key = COSEEC2Key::from_sec1_uncompressed(Curve::SECP256R1, &public_key)
            .map_err(|err| {
                CommandError::Deserializing(serde_parse_err(&format!("EC2 Key: {err:?}",)))
            })?;

        let credential_public_key = COSEKey {
            alg: COSEAlgorithm::ES256,
            key: COSEKeyType::EC2(credential_ec2_key),
        };

        let auth_data = AuthenticatorData {
            rp_id_hash: rp_id_hash.clone(),
            // https://fidoalliance.org/specs/fido-v2.0-ps-20190130/fido-client-to-authenticator-protocol-v2.0-ps-20190130.html#u2f-authenticatorMakeCredential-interoperability
            // "Let flags be a byte whose zeroth bit (bit 0, UP) is set, and whose sixth bit
            // (bit 6, AT) is set, and all other bits are zero (bit zero is the least
            // significant bit)"
            flags: AuthenticatorDataFlags::USER_PRESENT | AuthenticatorDataFlags::ATTESTED,
            counter: 0,
            credential_data: Some(AttestedCredentialData {
                aaguid: AAGuid::default(),
                credential_id,
                credential_public_key,
            }),
            extensions: Default::default(),
        };

        let att_stmt = AttestationStatement::FidoU2F(AttestationStatementFidoU2F::new(
            cert_and_sig.certificate,
            cert_and_sig.signature,
        ));

        let att_obj = AttestationObject {
            auth_data,
            att_stmt,
        };

        Ok(Self {
            att_obj,
            attachment: AuthenticatorAttachment::Unknown,
            extensions: Default::default(),
            ep_attestation: None,
            large_blob_key: None,
            unsigned_extension_outputs: None,
        })
    }
}

impl<'de> Deserialize<'de> for MakeCredentialsResult {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct MakeCredentialsResultVisitor;

        impl<'de> Visitor<'de> for MakeCredentialsResultVisitor {
            type Value = MakeCredentialsResult;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a cbor map")
            }

            fn visit_map<M>(self, mut map: M) -> Result<Self::Value, M::Error>
            where
                M: MapAccess<'de>,
            {
                let mut format: Option<&str> = None;
                let mut auth_data: Option<AuthenticatorData> = None;
                let mut att_stmt: Option<AttestationStatement> = None;
                let mut ep_attestation: Option<bool> = None;
                let mut large_blob_key: Option<Vec<u8>> = None;
                let mut unsigned_extension_outputs: Option<HashMap<String, serde_cbor::Value>> =
                    None;

                while let Some(key) = map.next_key()? {
                    match key {
                        0x01 => {
                            if format.is_some() {
                                return Err(DesError::duplicate_field("fmt (0x01)"));
                            }
                            format = Some(map.next_value()?);
                        }
                        0x02 => {
                            if auth_data.is_some() {
                                return Err(DesError::duplicate_field("authData (0x02)"));
                            }
                            auth_data = Some(map.next_value()?);
                        }
                        0x03 => {
                            let format =
                                format.ok_or_else(|| DesError::missing_field("fmt (0x01)"))?;
                            if att_stmt.is_some() {
                                return Err(DesError::duplicate_field("attStmt (0x03)"));
                            }
                            att_stmt = match format {
                                "none" => {
                                    let map: std::collections::BTreeMap<(), ()> =
                                        map.next_value()?;
                                    if !map.is_empty() {
                                        return Err(DesError::invalid_value(
                                            Unexpected::Map,
                                            &"the empty map",
                                        ));
                                    }
                                    Some(AttestationStatement::None)
                                }
                                "packed" => Some(AttestationStatement::Packed(map.next_value()?)),
                                "fido-u2f" => {
                                    Some(AttestationStatement::FidoU2F(map.next_value()?))
                                }
                                _ => {
                                    return Err(DesError::custom(
                                        "unknown attestation statement format",
                                    ));
                                }
                            }
                        }
                        0x04 => {
                            if ep_attestation.is_some() {
                                return Err(M::Error::duplicate_field("ep_attestation"));
                            }
                            let ep_attestation_val: bool = map.next_value()?;
                            ep_attestation = Some(ep_attestation_val);
                        }
                        0x05 => {
                            if large_blob_key.is_some() {
                                return Err(M::Error::duplicate_field("large_blob_key"));
                            }
                            let large_blob_key_bytes: ByteBuf = map.next_value()?;
                            large_blob_key = Some(large_blob_key_bytes.into_vec());
                        }
                        0x06 => {
                            if unsigned_extension_outputs.is_some() {
                                return Err(M::Error::duplicate_field(
                                    "unsigned_extension_outputs",
                                ));
                            }
                            unsigned_extension_outputs = Some(map.next_value()?);
                        }
                        _ => continue,
                    }
                }

                let auth_data = auth_data
                    .ok_or_else(|| M::Error::custom("found no authData (0x02)".to_string()))?;
                let att_stmt = att_stmt
                    .ok_or_else(|| M::Error::custom("found no attStmt (0x03)".to_string()))?;

                Ok(MakeCredentialsResult {
                    att_obj: AttestationObject {
                        auth_data,
                        att_stmt,
                    },
                    attachment: AuthenticatorAttachment::Unknown,
                    extensions: Default::default(),
                    ep_attestation,
                    large_blob_key,
                    unsigned_extension_outputs,
                })
            }
        }

        deserializer.deserialize_bytes(MakeCredentialsResultVisitor)
    }
}

impl CtapResponse for MakeCredentialsResult {}

#[derive(Copy, Clone, Debug, Default, Serialize)]
#[cfg_attr(test, derive(Deserialize))]
pub struct MakeCredentialsOptions {
    #[serde(rename = "rk", skip_serializing_if = "Option::is_none")]
    pub resident_key: Option<bool>,
    #[serde(rename = "uv", skip_serializing_if = "Option::is_none")]
    pub user_verification: Option<bool>,
    // TODO(MS): ctap2.1 supports user_presence, but ctap2.0 does not and tokens will error out
    //           Commands need a version-flag to know what to de/serialize and what to ignore.
}

impl MakeCredentialsOptions {
    pub(crate) fn has_some(&self) -> bool {
        self.resident_key.is_some() || self.user_verification.is_some()
    }
}

pub(crate) trait UserVerification {
    fn ask_user_verification(&self) -> bool;
}

impl UserVerification for MakeCredentialsOptions {
    fn ask_user_verification(&self) -> bool {
        self.user_verification.unwrap_or(false)
    }
}

#[derive(Debug, Default, Clone, Serialize)]
pub struct MakeCredentialsExtensions {
    #[serde(skip_serializing)]
    pub cred_props: Option<bool>,
    #[serde(rename = "credProtect", skip_serializing_if = "Option::is_none")]
    pub cred_protect: Option<CredentialProtectionPolicy>,
    #[serde(rename = "hmac-secret", skip_serializing_if = "Option::is_none")]
    pub hmac_secret: Option<HmacCreateSecretOrPrf>,
    #[serde(rename = "minPinLength", skip_serializing_if = "Option::is_none")]
    pub min_pin_length: Option<bool>,
    #[serde(rename = "credBlob", skip_serializing_if = "Option::is_none")]
    pub cred_blob: Option<AuthenticatorExtensionsCredBlob>,
    #[serde(rename = "largeBlobKey", skip_serializing_if = "Option::is_none")]
    pub large_blob_key: Option<bool>,
    #[serde(rename = "thirdPartyPayment", skip_serializing_if = "Option::is_none")]
    pub third_party_payment: Option<bool>,
}

#[derive(Debug, Clone)]
pub enum HmacCreateSecretOrPrf {
    HmacCreateSecret(bool),
    Prf,
}

impl Serialize for HmacCreateSecretOrPrf {
    fn serialize<S>(&self, s: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Self::HmacCreateSecret(hmac_secret) => s.serialize_bool(*hmac_secret),
            Self::Prf => s.serialize_bool(true),
        }
    }
}

impl MakeCredentialsExtensions {
    fn has_content(&self) -> bool {
        self.cred_protect.is_some()
            || self.hmac_secret.is_some()
            || self.min_pin_length.is_some()
            || self.cred_blob.is_some()
            || self.large_blob_key.is_some()
            || self.third_party_payment.is_some()
    }
}

impl From<AuthenticationExtensionsClientInputs> for MakeCredentialsExtensions {
    fn from(input: AuthenticationExtensionsClientInputs) -> Self {
        Self {
            cred_props: input.cred_props,
            cred_protect: input.credential_protection_policy,
            hmac_secret: match (input.hmac_create_secret, input.prf) {
                (None, None) => None,
                (_, Some(_)) => Some(HmacCreateSecretOrPrf::Prf),
                (Some(hmac_secret), _) => {
                    Some(HmacCreateSecretOrPrf::HmacCreateSecret(hmac_secret))
                }
            },
            min_pin_length: input.min_pin_length,
            cred_blob: input.cred_blob,
            large_blob_key: input.large_blob_key,
            third_party_payment: input.third_party_payment,
        }
    }
}

#[derive(Debug, Clone)]
pub struct MakeCredentials {
    pub client_data_hash: ClientDataHash,
    pub rp: RelyingParty,
    // Note(baloo): If none -> ctap1
    pub user: Option<PublicKeyCredentialUserEntity>,
    pub pub_cred_params: Vec<PublicKeyCredentialParameters>,
    pub exclude_list: Vec<PublicKeyCredentialDescriptor>,

    // https://www.w3.org/TR/webauthn/#client-extension-input
    // The client extension input, which is a value that can be encoded in JSON,
    // is passed from the WebAuthn Relying Party to the client in the get() or
    // create() call, while the CBOR authenticator extension input is passed
    // from the client to the authenticator for authenticator extensions during
    // the processing of these calls.
    pub extensions: MakeCredentialsExtensions,
    pub options: MakeCredentialsOptions,
    pub pin_uv_auth_param: Option<PinUvAuthParam>,
    pub enterprise_attestation: Option<u64>,

    // CTAP 2.2
    pub attestation_formats_preference: Option<Vec<String>>,
}

impl MakeCredentials {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        client_data_hash: ClientDataHash,
        rp: RelyingParty,
        user: Option<PublicKeyCredentialUserEntity>,
        pub_cred_params: Vec<PublicKeyCredentialParameters>,
        exclude_list: Vec<PublicKeyCredentialDescriptor>,
        options: MakeCredentialsOptions,
        extensions: MakeCredentialsExtensions,
    ) -> Self {
        Self {
            client_data_hash,
            rp,
            user,
            pub_cred_params,
            exclude_list,
            extensions,
            options,
            pin_uv_auth_param: None,
            enterprise_attestation: None,
            attestation_formats_preference: None,
        }
    }

    pub fn finalize_result<Dev: FidoDevice>(&self, dev: &Dev, result: &mut MakeCredentialsResult) {
        let maybe_info = dev.get_authenticator_info();

        result.attachment = match maybe_info {
            Some(info) if info.options.platform_device => AuthenticatorAttachment::Platform,
            Some(_) => AuthenticatorAttachment::CrossPlatform,
            None => AuthenticatorAttachment::Unknown,
        };

        // Handle extensions whose outputs are not encoded in the authenticator data.
        // 1. credProps
        //      "set clientExtensionResults["credProps"]["rk"] to the value of the
        //      requireResidentKey parameter that was used in the invocation of the
        //      authenticatorMakeCredential operation."
        //      Note: a CTAP 2.0 authenticator is allowed to create a discoverable credential even
        //      if one was not requested, so there is a case in which we cannot confidently
        //      return `rk=false` here. We omit the response entirely in this case.
        let dev_supports_rk = maybe_info.is_some_and(|info| info.options.resident_key);
        let requested_rk = self.options.resident_key.unwrap_or(false);
        let max_supported_version = maybe_info.map_or(AuthenticatorVersion::U2F_V2, |info| {
            info.max_supported_version()
        });
        let rk_uncertain = max_supported_version == AuthenticatorVersion::FIDO_2_0
            && dev_supports_rk
            && !requested_rk;
        if self.extensions.cred_props == Some(true) && !rk_uncertain {
            result
                .extensions
                .cred_props
                .get_or_insert(Default::default())
                .rk = requested_rk;
        }

        // 2. hmac-secret
        //      The extension returns a flag in the authenticator data which we need to mirror as a
        //      client output.
        // 3. prf
        //      hmac-secret returns a flag in the authenticator data
        //      which we need to mirror as a PRF "enabled" client output.
        //      If a future version of hmac-secret permits calculating secrets in makeCredential,
        //      we also need to decrypt and output them as client outputs.
        match self.extensions.hmac_secret {
            Some(HmacCreateSecretOrPrf::HmacCreateSecret(true)) => {
                result.extensions.hmac_create_secret =
                    Some(match result.att_obj.auth_data.extensions.hmac_secret {
                        Some(HmacSecretResponse::Confirmed(flag)) => flag,
                        Some(HmacSecretResponse::Secret(_)) => true,
                        None => false,
                    });
            }
            Some(HmacCreateSecretOrPrf::Prf) => {
                result.extensions.prf =
                    Some(match &result.att_obj.auth_data.extensions.hmac_secret {
                        None => AuthenticationExtensionsPRFOutputs {
                            enabled: Some(false),
                            results: None,
                        },
                        Some(HmacSecretResponse::Confirmed(flag)) => {
                            AuthenticationExtensionsPRFOutputs {
                                enabled: Some(*flag),
                                results: None,
                            }
                        }
                        Some(hmac_response @ HmacSecretResponse::Secret(_)) => {
                            AuthenticationExtensionsPRFOutputs {
                                enabled: Some(true),
                                results: dev
                                    .get_shared_secret()
                                    .and_then(|shared_secret| {
                                        hmac_response.decrypt_secrets(shared_secret)
                                    })
                                    .and_then(Result::ok)
                                    .map(|outputs| outputs.into()),
                            }
                        }
                    })
            }
            None | Some(HmacCreateSecretOrPrf::HmacCreateSecret(false)) => {}
        }

        // 3. credBlob
        //      The extension returns a flag in the authenticator data which we need to mirror as a
        //      client output.
        result.extensions.cred_blob = result.att_obj.auth_data.extensions.cred_blob.clone();
    }
}

impl PinUvAuthCommand for MakeCredentials {
    fn set_pin_uv_auth_param(
        &mut self,
        pin_uv_auth_token: Option<PinUvAuthToken>,
    ) -> Result<(), AuthenticatorError> {
        let mut param = None;
        if let Some(token) = pin_uv_auth_token {
            param = Some(
                token
                    .derive(self.client_data_hash.as_ref())
                    .map_err(CommandError::Crypto)?,
            );
        }
        self.pin_uv_auth_param = param;
        Ok(())
    }

    fn set_uv_option(&mut self, uv: Option<bool>) {
        self.options.user_verification = uv;
    }

    fn get_rp_id(&self) -> Option<&String> {
        Some(&self.rp.id)
    }

    fn can_skip_user_verification(
        &mut self,
        info: &AuthenticatorInfo,
        uv_req: UserVerificationRequirement,
    ) -> bool {
        // TODO(MS): Handle here the case where we NEED a UV, the device supports PINs, but hasn't set a PIN.
        //           For this, the user has to be prompted to set a PIN first (see https://github.com/mozilla/authenticator-rs/issues/223)

        let supports_uv = info.options.user_verification == Some(true);
        let pin_configured = info.options.client_pin == Some(true);

        // CTAP 2.0 authenticators require user verification if the device is protected
        let device_protected = supports_uv || pin_configured;

        // CTAP 2.1 authenticators may allow the creation of non-discoverable credentials without
        // user verification. This is only relevant if the relying party has not requested user
        // verification.
        let make_cred_uv_not_required = info.options.make_cred_uv_not_rqd == Some(true)
            && self.options.resident_key != Some(true)
            && uv_req == UserVerificationRequirement::Discouraged;

        // Alternatively, CTAP 2.1 authenticators may require user verification regardless of the
        // RP's requirement.
        let always_uv = info.options.always_uv == Some(true);

        !always_uv && (!device_protected || make_cred_uv_not_required)
    }

    fn get_pin_uv_auth_param(&self) -> Option<&PinUvAuthParam> {
        self.pin_uv_auth_param.as_ref()
    }

    fn hmac_requested(&self) -> bool {
        !(self.extensions.hmac_secret.is_none()
            || matches!(
                self.extensions.hmac_secret,
                Some(HmacCreateSecretOrPrf::HmacCreateSecret(false))
            ))
    }
}

impl Serialize for MakeCredentials {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        debug!("Serialize MakeCredentials");
        serialize_map_optional!(
            serializer,
            &0x01 => Some(&self.client_data_hash),
            &0x02 => Some(&self.rp),
            &0x03 => Some(&self.user),
            &0x04 => Some(&self.pub_cred_params),
            &0x05 => (!self.exclude_list.is_empty()).then_some(&self.exclude_list),
            &0x06 => self.extensions.has_content().then_some(&self.extensions),
            &0x07 => self.options.has_some().then_some(&self.options),
            &0x08 => &self.pin_uv_auth_param,
            &0x09 => self.pin_uv_auth_param.as_ref().map(|p| p.pin_protocol.id()),
            &0x0a => &self.enterprise_attestation,
            &0x0b => &self.attestation_formats_preference,
        )
    }
}

impl RequestCtap1 for MakeCredentials {
    type Output = MakeCredentialsResult;
    type AdditionalInfo = ();

    fn ctap1_format(&self) -> Result<(Vec<u8>, ()), HIDError> {
        let flags = U2F_REQUEST_USER_PRESENCE;

        let mut register_data = Vec::with_capacity(2 * PARAMETER_SIZE);
        register_data.extend_from_slice(self.client_data_hash.as_ref());
        register_data.extend_from_slice(self.rp.hash().as_ref());
        let cmd = U2F_REGISTER;
        let apdu = CTAP1RequestAPDU::serialize(cmd, flags, &register_data)?;

        Ok((apdu, ()))
    }

    fn handle_response_ctap1<Dev: FidoDevice>(
        &self,
        dev: &mut Dev,
        status: Result<(), ApduErrorStatus>,
        input: &[u8],
        _add_info: &(),
    ) -> Result<Self::Output, Retryable<HIDError>> {
        if Err(ApduErrorStatus::ConditionsNotSatisfied) == status {
            return Err(Retryable::Retry);
        }
        if let Err(err) = status {
            return Err(Retryable::Error(HIDError::ApduStatus(err)));
        }

        let mut output = MakeCredentialsResult::from_ctap1(input, &self.rp.hash())
            .map_err(|e| Retryable::Error(HIDError::Command(e)))?;
        self.finalize_result(dev, &mut output);
        Ok(output)
    }

    fn send_to_virtual_device<Dev: VirtualFidoDevice>(
        &self,
        dev: &mut Dev,
    ) -> Result<Self::Output, HIDError> {
        let mut output = dev.make_credentials(self)?;
        self.finalize_result(dev, &mut output);
        Ok(output)
    }
}

impl RequestCtap2 for MakeCredentials {
    type Output = MakeCredentialsResult;

    fn command(&self) -> Command {
        Command::MakeCredentials
    }

    fn wire_format(&self) -> Result<Vec<u8>, HIDError> {
        Ok(ser::to_vec(&self).map_err(CommandError::Serializing)?)
    }

    fn handle_response_ctap2<Dev: FidoDevice>(
        &self,
        dev: &mut Dev,
        input: &[u8],
    ) -> Result<Self::Output, HIDError> {
        if input.is_empty() {
            return Err(HIDError::Command(CommandError::InputTooSmall));
        }

        let status: StatusCode = input[0].into();
        debug!("response status code: {:?}", status);
        if input.len() == 1 {
            if status.is_ok() {
                return Err(HIDError::Command(CommandError::InputTooSmall));
            }
            return Err(HIDError::Command(CommandError::StatusCode(status, None)));
        }

        if status.is_ok() {
            let mut output: MakeCredentialsResult =
                from_slice(&input[1..]).map_err(CommandError::Deserializing)?;
            self.finalize_result(dev, &mut output);
            Ok(output)
        } else {
            let data: Value = from_slice(&input[1..]).map_err(CommandError::Deserializing)?;
            Err(HIDError::Command(CommandError::StatusCode(
                status,
                Some(data),
            )))
        }
    }

    fn send_to_virtual_device<Dev: VirtualFidoDevice>(
        &self,
        dev: &mut Dev,
    ) -> Result<Self::Output, HIDError> {
        let mut output = dev.make_credentials(self)?;
        self.finalize_result(dev, &mut output);
        Ok(output)
    }
}

pub(crate) fn dummy_make_credentials_cmd() -> MakeCredentials {
    let mut req = MakeCredentials::new(
        // Hardcoded hash of:
        // CollectedClientData {
        //     webauthn_type: WebauthnType::Create,
        //     challenge: Challenge::new(vec![0, 1, 2, 3, 4]),
        //     origin: String::new(),
        //     cross_origin: false,
        //     token_binding: None,
        // }
        ClientDataHash([
            208, 206, 230, 252, 125, 191, 89, 154, 145, 157, 184, 251, 149, 19, 17, 38, 159, 14,
            183, 129, 247, 132, 28, 108, 192, 84, 74, 217, 218, 52, 21, 75,
        ]),
        RelyingParty::from("make.me.blink"),
        Some(PublicKeyCredentialUserEntity {
            id: vec![0],
            name: Some(String::from("make.me.blink")),
            ..Default::default()
        }),
        vec![PublicKeyCredentialParameters {
            alg: COSEAlgorithm::ES256,
        }],
        vec![],
        MakeCredentialsOptions::default(),
        MakeCredentialsExtensions::default(),
    );
    // Using a zero-length pinAuth will trigger the device to blink.
    // For CTAP1, this gets ignored anyways and we do a 'normal' register
    // command, which also just blinks.
    req.pin_uv_auth_param = Some(PinUvAuthParam::create_empty());
    req
}

#[cfg(test)]
pub mod test {
    use super::*;
    use crate::{
        crypto::{
            COSEAlgorithm, COSEEC2Key, COSEKey, COSEKeyType, Curve, PinUvAuthParam,
            PinUvAuthProtocol,
        },
        ctap2::{
            attestation::{
                test::create_attestation_obj, AAGuid, AttestationCertificate, AttestationObject,
                AttestationStatement, AttestationStatementFidoU2F, AttestedCredentialData,
                AuthenticatorData, AuthenticatorDataFlags, Signature,
            },
            client_data::{Challenge, CollectedClientData, TokenBinding, WebauthnType},
            commands::{
                assert_canonical_cbor_encoding,
                make_credentials::{HmacCreateSecretOrPrf, MakeCredentialsExtensions},
                RequestCtap1, RequestCtap2,
            },
            server::{
                AuthenticatorAttachment, PublicKeyCredentialDescriptor,
                PublicKeyCredentialParameters, PublicKeyCredentialUserEntity, RelyingParty,
                RpIdHash, Transport,
            },
        },
        transport::{device_selector::Device, hid::HIDDevice, FidoDevice, FidoProtocol},
        AuthenticatorInfo, MakeCredentialsResult,
    };
    use base64::Engine;
    use std::convert::TryFrom;

    #[test]
    fn test_make_credentials_ctap2() {
        let req = MakeCredentials::new(
            CollectedClientData {
                webauthn_type: WebauthnType::Create,
                challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
                origin: String::from("example.com"),
                cross_origin: false,
                token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
            }
            .hash()
            .expect("failed to serialize client data"),
            RelyingParty {
                id: String::from("example.com"),
                name: Some(String::from("Acme")),
            },
            Some(PublicKeyCredentialUserEntity {
                id: base64::engine::general_purpose::URL_SAFE
                    .decode("MIIBkzCCATigAwIBAjCCAZMwggE4oAMCAQIwggGTMII=")
                    .unwrap(),
                name: Some(String::from("johnpsmith@example.com")),
                display_name: Some(String::from("John P. Smith")),
            }),
            vec![
                PublicKeyCredentialParameters {
                    alg: COSEAlgorithm::ES256,
                },
                PublicKeyCredentialParameters {
                    alg: COSEAlgorithm::RS256,
                },
            ],
            Vec::new(),
            MakeCredentialsOptions {
                resident_key: Some(true),
                user_verification: None,
            },
            Default::default(),
        );
        assert_canonical_cbor_encoding(&req);

        let mut device = Device::new("commands/make_credentials").unwrap(); // not really used (all functions ignore it)
        assert_eq!(device.get_protocol(), FidoProtocol::CTAP2);
        let req_serialized = req
            .wire_format()
            .expect("Failed to serialize MakeCredentials request");
        assert_eq!(req_serialized, MAKE_CREDENTIALS_SAMPLE_REQUEST_CTAP2);
        let make_cred_result = req
            .handle_response_ctap2(&mut device, &MAKE_CREDENTIALS_SAMPLE_RESPONSE_CTAP2)
            .expect("Failed to handle CTAP2 response");

        let expected = MakeCredentialsResult {
            att_obj: create_attestation_obj(),
            attachment: AuthenticatorAttachment::Unknown,
            extensions: Default::default(),
            ep_attestation: None,
            large_blob_key: None,
            unsigned_extension_outputs: None,
        };

        assert_eq!(make_cred_result, expected);
    }

    #[test]
    fn test_serialize_make_credentials_ctap2_all() {
        let req = MakeCredentials {
            client_data_hash: CollectedClientData {
                webauthn_type: WebauthnType::Create,
                challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
                origin: String::from("example.com"),
                cross_origin: true,
                token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
            }
            .hash()
            .expect("failed to serialize client data"),
            rp: RelyingParty {
                id: String::from("example.com"),
                name: Some(String::from("Acme")),
            },
            user: Some(PublicKeyCredentialUserEntity {
                id: base64::engine::general_purpose::URL_SAFE
                    .decode("MIIBkzCCATigAwIBAjCCAZMwggE4oAMCAQIwggGTMII=")
                    .unwrap(),
                name: Some(String::from("johnpsmith@example.com")),
                display_name: Some(String::from("John P. Smith")),
            }),
            pub_cred_params: vec![
                PublicKeyCredentialParameters {
                    alg: COSEAlgorithm::ES256,
                },
                PublicKeyCredentialParameters {
                    alg: COSEAlgorithm::RS256,
                },
            ],
            exclude_list: vec![PublicKeyCredentialDescriptor {
                id: vec![4, 5, 6, 7],
                transports: vec![Transport::USB, Transport::NFC],
            }],
            extensions: MakeCredentialsExtensions {
                cred_props: Some(true),
                cred_protect: Some(
                    crate::ctap2::server::CredentialProtectionPolicy::UserVerificationRequired,
                ),
                hmac_secret: Some(HmacCreateSecretOrPrf::HmacCreateSecret(true)),
                min_pin_length: Some(true),
                cred_blob: None,
                large_blob_key: None,
                third_party_payment: None,
            },
            options: MakeCredentialsOptions {
                resident_key: Some(true),
                user_verification: Some(true),
            },
            pin_uv_auth_param: Some({
                let mut p = PinUvAuthParam::create_empty();
                p.pin_protocol = PinUvAuthProtocol::try_from(&AuthenticatorInfo {
                    pin_protocols: Some(vec![2]),
                    ..Default::default()
                })
                .expect("Failed to create PIN protocol");
                p
            }),
            enterprise_attestation: Some(7),
            attestation_formats_preference: None,
        };

        let req_serialized = req
            .wire_format()
            .expect("Failed to serialize MakeCredentials request");
        assert_eq!(
            req_serialized,
            [
                // Value copied from test failure output as regression test snapshot
                170, 1, 88, 32, 122, 146, 22, 162, 76, 198, 134, 9, 213, 180, 166, 87, 146, 132,
                116, 119, 180, 172, 60, 27, 146, 142, 207, 8, 158, 125, 4, 28, 226, 254, 21, 122,
                2, 162, 98, 105, 100, 107, 101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, 100,
                110, 97, 109, 101, 100, 65, 99, 109, 101, 3, 163, 98, 105, 100, 88, 32, 48, 130, 1,
                147, 48, 130, 1, 56, 160, 3, 2, 1, 2, 48, 130, 1, 147, 48, 130, 1, 56, 160, 3, 2,
                1, 2, 48, 130, 1, 147, 48, 130, 100, 110, 97, 109, 101, 118, 106, 111, 104, 110,
                112, 115, 109, 105, 116, 104, 64, 101, 120, 97, 109, 112, 108, 101, 46, 99, 111,
                109, 107, 100, 105, 115, 112, 108, 97, 121, 78, 97, 109, 101, 109, 74, 111, 104,
                110, 32, 80, 46, 32, 83, 109, 105, 116, 104, 4, 130, 162, 99, 97, 108, 103, 38,
                100, 116, 121, 112, 101, 106, 112, 117, 98, 108, 105, 99, 45, 107, 101, 121, 162,
                99, 97, 108, 103, 57, 1, 0, 100, 116, 121, 112, 101, 106, 112, 117, 98, 108, 105,
                99, 45, 107, 101, 121, 5, 129, 162, 98, 105, 100, 68, 4, 5, 6, 7, 100, 116, 121,
                112, 101, 106, 112, 117, 98, 108, 105, 99, 45, 107, 101, 121, 6, 163, 107, 99, 114,
                101, 100, 80, 114, 111, 116, 101, 99, 116, 3, 107, 104, 109, 97, 99, 45, 115, 101,
                99, 114, 101, 116, 245, 108, 109, 105, 110, 80, 105, 110, 76, 101, 110, 103, 116,
                104, 245, 7, 162, 98, 114, 107, 245, 98, 117, 118, 245, 8, 64, 9, 2, 10, 7
            ]
        );
    }

    #[test]
    fn test_make_credentials_ctap1() {
        let req = MakeCredentials::new(
            CollectedClientData {
                webauthn_type: WebauthnType::Create,
                challenge: Challenge::new(vec![0x00, 0x01, 0x02, 0x03]),
                origin: String::from("example.com"),
                cross_origin: false,
                token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
            }
            .hash()
            .expect("failed to serialize client data"),
            RelyingParty::from("example.com"),
            Some(PublicKeyCredentialUserEntity {
                id: base64::engine::general_purpose::URL_SAFE
                    .decode("MIIBkzCCATigAwIBAjCCAZMwggE4oAMCAQIwggGTMII=")
                    .unwrap(),
                name: Some(String::from("johnpsmith@example.com")),
                display_name: Some(String::from("John P. Smith")),
            }),
            vec![
                PublicKeyCredentialParameters {
                    alg: COSEAlgorithm::ES256,
                },
                PublicKeyCredentialParameters {
                    alg: COSEAlgorithm::RS256,
                },
            ],
            Vec::new(),
            MakeCredentialsOptions {
                resident_key: Some(true),
                user_verification: None,
            },
            Default::default(),
        );
        assert_canonical_cbor_encoding(&req);

        let (req_serialized, _) = req
            .ctap1_format()
            .expect("Failed to serialize MakeCredentials request");
        assert_eq!(
            req_serialized, MAKE_CREDENTIALS_SAMPLE_REQUEST_CTAP1,
            "\nGot:      {req_serialized:X?}\nExpected: {MAKE_CREDENTIALS_SAMPLE_REQUEST_CTAP1:X?}"
        );
        let mut device = Device::new("commands/make_credentials").unwrap(); // not really used
        let make_cred_result = req
            .handle_response_ctap1(
                &mut device,
                Ok(()),
                &MAKE_CREDENTIALS_SAMPLE_RESPONSE_CTAP1,
                &(),
            )
            .expect("Failed to handle CTAP1 response");

        let att_obj = AttestationObject {
            auth_data: AuthenticatorData {
                rp_id_hash: RpIdHash::from(&[
                    0xA3, 0x79, 0xA6, 0xF6, 0xEE, 0xAF, 0xB9, 0xA5, 0x5E, 0x37, 0x8C, 0x11, 0x80,
                    0x34, 0xE2, 0x75, 0x1E, 0x68, 0x2F, 0xAB, 0x9F, 0x2D, 0x30, 0xAB, 0x13, 0xD2,
                    0x12, 0x55, 0x86, 0xCE, 0x19, 0x47,
                ])
                .unwrap(),
                flags: AuthenticatorDataFlags::USER_PRESENT | AuthenticatorDataFlags::ATTESTED,
                counter: 0,
                credential_data: Some(AttestedCredentialData {
                    aaguid: AAGuid::default(),
                    credential_id: vec![
                        0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26,
                        0x35, 0xEF, 0xAA, 0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3,
                        0x71, 0x7D, 0xA4, 0x85, 0x34, 0xC8, 0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94,
                        0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD, 0x39, 0x6B, 0x64,
                        0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08,
                        0xFE, 0x42, 0x00, 0x38,
                    ],
                    credential_public_key: COSEKey {
                        alg: COSEAlgorithm::ES256,
                        key: COSEKeyType::EC2(COSEEC2Key {
                            curve: Curve::SECP256R1,
                            x: vec![
                                0xE8, 0x76, 0x25, 0x89, 0x6E, 0xE4, 0xE4, 0x6D, 0xC0, 0x32, 0x76,
                                0x6E, 0x80, 0x87, 0x96, 0x2F, 0x36, 0xDF, 0x9D, 0xFE, 0x8B, 0x56,
                                0x7F, 0x37, 0x63, 0x01, 0x5B, 0x19, 0x90, 0xA6, 0x0E, 0x14,
                            ],
                            y: vec![
                                0x27, 0xDE, 0x61, 0x2D, 0x66, 0x41, 0x8B, 0xDA, 0x19, 0x50, 0x58,
                                0x1E, 0xBC, 0x5C, 0x8C, 0x1D, 0xAD, 0x71, 0x0C, 0xB1, 0x4C, 0x22,
                                0xF8, 0xC9, 0x70, 0x45, 0xF4, 0x61, 0x2F, 0xB2, 0x0C, 0x91,
                            ],
                        }),
                    },
                }),
                extensions: Default::default(),
            },
            att_stmt: AttestationStatement::FidoU2F(AttestationStatementFidoU2F {
                sig: Signature(vec![
                    0x30, 0x45, 0x02, 0x20, 0x32, 0x47, 0x79, 0xC6, 0x8F, 0x33, 0x80, 0x28, 0x8A,
                    0x11, 0x97, 0xB6, 0x09, 0x5F, 0x7A, 0x6E, 0xB9, 0xB1, 0xB1, 0xC1, 0x27, 0xF6,
                    0x6A, 0xE1, 0x2A, 0x99, 0xFE, 0x85, 0x32, 0xEC, 0x23, 0xB9, 0x02, 0x21, 0x00,
                    0xE3, 0x95, 0x16, 0xAC, 0x4D, 0x61, 0xEE, 0x64, 0x04, 0x4D, 0x50, 0xB4, 0x15,
                    0xA6, 0xA4, 0xD4, 0xD8, 0x4B, 0xA6, 0xD8, 0x95, 0xCB, 0x5A, 0xB7, 0xA1, 0xAA,
                    0x7D, 0x08, 0x1D, 0xE3, 0x41, 0xFA,
                ]),
                attestation_cert: vec![AttestationCertificate(vec![
                    0x30, 0x82, 0x02, 0x4A, 0x30, 0x82, 0x01, 0x32, 0xA0, 0x03, 0x02, 0x01, 0x02,
                    0x02, 0x04, 0x04, 0x6C, 0x88, 0x22, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48,
                    0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B, 0x05, 0x00, 0x30, 0x2E, 0x31, 0x2C, 0x30,
                    0x2A, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x23, 0x59, 0x75, 0x62, 0x69, 0x63,
                    0x6F, 0x20, 0x55, 0x32, 0x46, 0x20, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x43, 0x41,
                    0x20, 0x53, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x20, 0x34, 0x35, 0x37, 0x32, 0x30,
                    0x30, 0x36, 0x33, 0x31, 0x30, 0x20, 0x17, 0x0D, 0x31, 0x34, 0x30, 0x38, 0x30,
                    0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5A, 0x18, 0x0F, 0x32, 0x30, 0x35,
                    0x30, 0x30, 0x39, 0x30, 0x34, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5A, 0x30,
                    0x2C, 0x31, 0x2A, 0x30, 0x28, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x21, 0x59,
                    0x75, 0x62, 0x69, 0x63, 0x6F, 0x20, 0x55, 0x32, 0x46, 0x20, 0x45, 0x45, 0x20,
                    0x53, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x20, 0x32, 0x34, 0x39, 0x31, 0x38, 0x32,
                    0x33, 0x32, 0x34, 0x37, 0x37, 0x30, 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A,
                    0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D,
                    0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x3C, 0xCA, 0xB9, 0x2C, 0xCB, 0x97,
                    0x28, 0x7E, 0xE8, 0xE6, 0x39, 0x43, 0x7E, 0x21, 0xFC, 0xD6, 0xB6, 0xF1, 0x65,
                    0xB2, 0xD5, 0xA3, 0xF3, 0xDB, 0x13, 0x1D, 0x31, 0xC1, 0x6B, 0x74, 0x2B, 0xB4,
                    0x76, 0xD8, 0xD1, 0xE9, 0x90, 0x80, 0xEB, 0x54, 0x6C, 0x9B, 0xBD, 0xF5, 0x56,
                    0xE6, 0x21, 0x0F, 0xD4, 0x27, 0x85, 0x89, 0x9E, 0x78, 0xCC, 0x58, 0x9E, 0xBE,
                    0x31, 0x0F, 0x6C, 0xDB, 0x9F, 0xF4, 0xA3, 0x3B, 0x30, 0x39, 0x30, 0x22, 0x06,
                    0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0xC4, 0x0A, 0x02, 0x04, 0x15, 0x31,
                    0x2E, 0x33, 0x2E, 0x36, 0x2E, 0x31, 0x2E, 0x34, 0x2E, 0x31, 0x2E, 0x34, 0x31,
                    0x34, 0x38, 0x32, 0x2E, 0x31, 0x2E, 0x32, 0x30, 0x13, 0x06, 0x0B, 0x2B, 0x06,
                    0x01, 0x04, 0x01, 0x82, 0xE5, 0x1C, 0x02, 0x01, 0x01, 0x04, 0x04, 0x03, 0x02,
                    0x04, 0x30, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01,
                    0x01, 0x0B, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x9F, 0x9B, 0x05, 0x22,
                    0x48, 0xBC, 0x4C, 0xF4, 0x2C, 0xC5, 0x99, 0x1F, 0xCA, 0xAB, 0xAC, 0x9B, 0x65,
                    0x1B, 0xBE, 0x5B, 0xDC, 0xDC, 0x8E, 0xF0, 0xAD, 0x2C, 0x1C, 0x1F, 0xFB, 0x36,
                    0xD1, 0x87, 0x15, 0xD4, 0x2E, 0x78, 0xB2, 0x49, 0x22, 0x4F, 0x92, 0xC7, 0xE6,
                    0xE7, 0xA0, 0x5C, 0x49, 0xF0, 0xE7, 0xE4, 0xC8, 0x81, 0xBF, 0x2E, 0x94, 0xF4,
                    0x5E, 0x4A, 0x21, 0x83, 0x3D, 0x74, 0x56, 0x85, 0x1D, 0x0F, 0x6C, 0x14, 0x5A,
                    0x29, 0x54, 0x0C, 0x87, 0x4F, 0x30, 0x92, 0xC9, 0x34, 0xB4, 0x3D, 0x22, 0x2B,
                    0x89, 0x62, 0xC0, 0xF4, 0x10, 0xCE, 0xF1, 0xDB, 0x75, 0x89, 0x2A, 0xF1, 0x16,
                    0xB4, 0x4A, 0x96, 0xF5, 0xD3, 0x5A, 0xDE, 0xA3, 0x82, 0x2F, 0xC7, 0x14, 0x6F,
                    0x60, 0x04, 0x38, 0x5B, 0xCB, 0x69, 0xB6, 0x5C, 0x99, 0xE7, 0xEB, 0x69, 0x19,
                    0x78, 0x67, 0x03, 0xC0, 0xD8, 0xCD, 0x41, 0xE8, 0xF7, 0x5C, 0xCA, 0x44, 0xAA,
                    0x8A, 0xB7, 0x25, 0xAD, 0x8E, 0x79, 0x9F, 0xF3, 0xA8, 0x69, 0x6A, 0x6F, 0x1B,
                    0x26, 0x56, 0xE6, 0x31, 0xB1, 0xE4, 0x01, 0x83, 0xC0, 0x8F, 0xDA, 0x53, 0xFA,
                    0x4A, 0x8F, 0x85, 0xA0, 0x56, 0x93, 0x94, 0x4A, 0xE1, 0x79, 0xA1, 0x33, 0x9D,
                    0x00, 0x2D, 0x15, 0xCA, 0xBD, 0x81, 0x00, 0x90, 0xEC, 0x72, 0x2E, 0xF5, 0xDE,
                    0xF9, 0x96, 0x5A, 0x37, 0x1D, 0x41, 0x5D, 0x62, 0x4B, 0x68, 0xA2, 0x70, 0x7C,
                    0xAD, 0x97, 0xBC, 0xDD, 0x17, 0x85, 0xAF, 0x97, 0xE2, 0x58, 0xF3, 0x3D, 0xF5,
                    0x6A, 0x03, 0x1A, 0xA0, 0x35, 0x6D, 0x8E, 0x8D, 0x5E, 0xBC, 0xAD, 0xC7, 0x4E,
                    0x07, 0x16, 0x36, 0xC6, 0xB1, 0x10, 0xAC, 0xE5, 0xCC, 0x9B, 0x90, 0xDF, 0xEA,
                    0xCA, 0xE6, 0x40, 0xFF, 0x1B, 0xB0, 0xF1, 0xFE, 0x5D, 0xB4, 0xEF, 0xF7, 0xA9,
                    0x5F, 0x06, 0x07, 0x33, 0xF5,
                ])],
            }),
        };

        let expected = MakeCredentialsResult {
            att_obj,
            attachment: AuthenticatorAttachment::Unknown,
            extensions: Default::default(),
            ep_attestation: None,
            large_blob_key: None,
            unsigned_extension_outputs: None,
        };

        assert_eq!(make_cred_result, expected);
    }

    // This includes a CTAP2 encoded attestation object that is identical to
    // the WebAuthn encoded attestation object in `ctap2::attestation::test::SAMPLE_ATTESTATION`.
    // Both values decode to `ctap2::attestation::test::create_attestation_obj`.
    #[rustfmt::skip]
    pub const MAKE_CREDENTIALS_SAMPLE_RESPONSE_CTAP2: [u8; 660] = [
        0x00, // status = success
        0xa3, // map(3)
          0x01, // unsigned(1)
          0x66, // text(6)
            0x70, 0x61, 0x63, 0x6b, 0x65, 0x64, // "packed"
          0x02, // unsigned(2)
          0x58, 0x94, // bytes(148)
            // authData
            0xc2, 0x89, 0xc5, 0xca, 0x9b, 0x04, 0x60, 0xf9, 0x34, 0x6a, 0xb4, 0xe4, 0x2d, 0x84, 0x27, // rp_id_hash
            0x43, 0x40, 0x4d, 0x31, 0xf4, 0x84, 0x68, 0x25, 0xa6, 0xd0, 0x65, 0xbe, 0x59, 0x7a, 0x87, // rp_id_hash
            0x05, 0x1d, // rp_id_hash
            0x41, // authData Flags
            0x00, 0x00, 0x00, 0x0b, // authData counter
            0xf8, 0xa0, 0x11, 0xf3, 0x8c, 0x0a, 0x4d, 0x15, 0x80, 0x06, 0x17, 0x11, 0x1f, 0x9e, 0xdc, 0x7d, // AAGUID
            0x00, 0x10, // credential id length
            0x89, 0x59, 0xce, 0xad, 0x5b, 0x5c, 0x48, 0x16, 0x4e, 0x8a, 0xbc, 0xd6, 0xd9, 0x43, 0x5c, 0x6f, // credential id
            // credential public key
            0xa5, 0x01, 0x02, 0x03, 0x26, 0x20, 0x01, 0x21, 0x58, 0x20, 0xa5, 0xfd, 0x5c, 0xe1, 0xb1, 0xc4,
             0x58, 0xc5, 0x30, 0xa5, 0x4f, 0xa6, 0x1b, 0x31, 0xbf, 0x6b, 0x04, 0xbe, 0x8b, 0x97, 0xaf, 0xde,
             0x54, 0xdd, 0x8c, 0xbb, 0x69, 0x27, 0x5a, 0x8a, 0x1b, 0xe1, 0x22, 0x58, 0x20, 0xfa, 0x3a, 0x32,
             0x31, 0xdd, 0x9d, 0xee, 0xd9, 0xd1, 0x89, 0x7b, 0xe5, 0xa6, 0x22, 0x8c, 0x59, 0x50, 0x1e, 0x4b,
             0xcd, 0x12, 0x97, 0x5d, 0x3d, 0xff, 0x73, 0x0f, 0x01, 0x27, 0x8e, 0xa6, 0x1c,
          0x03, // unsigned(3)
          0xa3, // map(3)
            0x63, // text(3)
              0x61, 0x6c, 0x67, // "alg"
            0x26, // -7 (ES256)
            0x63, // text(3)
              0x73, 0x69, 0x67, // "sig"
            0x58, 0x47, // bytes(71)
              0x30, 0x45, 0x02, 0x20, 0x13, 0xf7, 0x3c, 0x5d, 0x9d, 0x53, 0x0e, 0x8c, 0xc1, 0x5c, 0xc9, // signature
              0xbd, 0x96, 0xad, 0x58, 0x6d, 0x39, 0x36, 0x64, 0xe4, 0x62, 0xd5, 0xf0, 0x56, 0x12, 0x35, // ..
              0xe6, 0x35, 0x0f, 0x2b, 0x72, 0x89, 0x02, 0x21, 0x00, 0x90, 0x35, 0x7f, 0xf9, 0x10, 0xcc, // ..
              0xb5, 0x6a, 0xc5, 0xb5, 0x96, 0x51, 0x19, 0x48, 0x58, 0x1c, 0x8f, 0xdd, 0xb4, 0xa2, 0xb7, // ..
              0x99, 0x59, 0x94, 0x80, 0x78, 0xb0, 0x9f, 0x4b, 0xdc, 0x62, 0x29, // ..
            0x63, // text(3)
              0x78, 0x35, 0x63, // "x5c"
            0x81, // array(1)
              0x59, 0x01, 0x97, // bytes(407)
                0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, //certificate...
                0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x09, 0x00, 0x85, 0x9b, 0x72, 0x6c, 0xb2, 0x4b,
                0x4c, 0x29, 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x30,
                0x47, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31,
                0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0b, 0x59, 0x75, 0x62, 0x69, 0x63,
                0x6f, 0x20, 0x54, 0x65, 0x73, 0x74, 0x31, 0x22, 0x30, 0x20, 0x06, 0x03, 0x55, 0x04, 0x0b,
                0x0c, 0x19, 0x41, 0x75, 0x74, 0x68, 0x65, 0x6e, 0x74, 0x69, 0x63, 0x61, 0x74, 0x6f, 0x72,
                0x20, 0x41, 0x74, 0x74, 0x65, 0x73, 0x74, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x30, 0x1e, 0x17,
                0x0d, 0x31, 0x36, 0x31, 0x32, 0x30, 0x34, 0x31, 0x31, 0x35, 0x35, 0x30, 0x30, 0x5a, 0x17,
                0x0d, 0x32, 0x36, 0x31, 0x32, 0x30, 0x32, 0x31, 0x31, 0x35, 0x35, 0x30, 0x30, 0x5a, 0x30,
                0x47, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31,
                0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0b, 0x59, 0x75, 0x62, 0x69, 0x63,
                0x6f, 0x20, 0x54, 0x65, 0x73, 0x74, 0x31, 0x22, 0x30, 0x20, 0x06, 0x03, 0x55, 0x04, 0x0b,
                0x0c, 0x19, 0x41, 0x75, 0x74, 0x68, 0x65, 0x6e, 0x74, 0x69, 0x63, 0x61, 0x74, 0x6f, 0x72,
                0x20, 0x41, 0x74, 0x74, 0x65, 0x73, 0x74, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x30, 0x59, 0x30,
                0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
                0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0xad, 0x11, 0xeb, 0x0e, 0x88, 0x52,
                0xe5, 0x3a, 0xd5, 0xdf, 0xed, 0x86, 0xb4, 0x1e, 0x61, 0x34, 0xa1, 0x8e, 0xc4, 0xe1, 0xaf,
                0x8f, 0x22, 0x1a, 0x3c, 0x7d, 0x6e, 0x63, 0x6c, 0x80, 0xea, 0x13, 0xc3, 0xd5, 0x04, 0xff,
                0x2e, 0x76, 0x21, 0x1b, 0xb4, 0x45, 0x25, 0xb1, 0x96, 0xc4, 0x4c, 0xb4, 0x84, 0x99, 0x79,
                0xcf, 0x6f, 0x89, 0x6e, 0xcd, 0x2b, 0xb8, 0x60, 0xde, 0x1b, 0xf4, 0x37, 0x6b, 0xa3, 0x0d,
                0x30, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x04, 0x02, 0x30, 0x00, 0x30, 0x0a,
                0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03, 0x49, 0x00, 0x30, 0x46,
                0x02, 0x21, 0x00, 0xe9, 0xa3, 0x9f, 0x1b, 0x03, 0x19, 0x75, 0x25, 0xf7, 0x37, 0x3e, 0x10,
                0xce, 0x77, 0xe7, 0x80, 0x21, 0x73, 0x1b, 0x94, 0xd0, 0xc0, 0x3f, 0x3f, 0xda, 0x1f, 0xd2,
                0x2d, 0xb3, 0xd0, 0x30, 0xe7, 0x02, 0x21, 0x00, 0xc4, 0xfa, 0xec, 0x34, 0x45, 0xa8, 0x20,
                0xcf, 0x43, 0x12, 0x9c, 0xdb, 0x00, 0xaa, 0xbe, 0xfd, 0x9a, 0xe2, 0xd8, 0x74, 0xf9, 0xc5,
                0xd3, 0x43, 0xcb, 0x2f, 0x11, 0x3d, 0xa2, 0x37, 0x23, 0xf3,
    ];

    #[rustfmt::skip]
    pub const MAKE_CREDENTIALS_SAMPLE_REQUEST_CTAP2: [u8; 210] = [
        // NOTE: This has been taken from CTAP2.0 spec, but the clientDataHash has been replaced
        //       to be able to operate with known values for CollectedClientData (spec doesn't say
        //       what values led to the provided example hash (see client_data.rs))
        0xa5, // map(5)
          0x01, // unsigned(1) - clientDataHash
          0x58, 0x20, // bytes(32)
            0x75, 0x35, 0x35, 0x7d, 0x49, 0x6e, 0x33, 0xc8, 0x18, 0x7f, 0xea, 0x8d, 0x11, // hash
            0x32, 0x64, 0xaa, 0xa4, 0x52, 0x3e, 0x13, 0x40, 0x14, 0x9f, 0xbe, 0x00, 0x3f, // hash
            0x10, 0x87, 0x54, 0xc3, 0x2d, 0x80, // hash
          0x02, // unsigned(2) - rp
            0xa2, // map(2) Replace line below with this one, once RelyingParty supports "name"
              0x62, // text(2)
                0x69, 0x64, // "id"
              0x6b, // text(11)
                0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, // "example.com"
              0x64, // text(4)
                0x6e, 0x61, 0x6d, 0x65, // "name"
              0x64, // text(4)
                0x41, 0x63, 0x6d, 0x65, // "Acme"
          0x03, // unsigned(3) - user
          0xa3, // map(3)
            0x62, // text(2)
              0x69, 0x64, // "id"
            0x58, 0x20, // bytes(32)
              0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, // userid
              0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02, // ...
              0x30, 0x82, 0x01, 0x93, 0x30, 0x82, // ...
            0x64, // text(4)
              0x6e, 0x61, 0x6d, 0x65, // "name"
            0x76, // text(22)
              0x6a, 0x6f, 0x68, 0x6e, 0x70, 0x73, 0x6d, 0x69, 0x74, // "johnpsmith@example.com"
              0x68, 0x40, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, // ...
            0x6b, // text(11)
              0x64, 0x69, 0x73, 0x70, 0x6c, 0x61, 0x79, 0x4e, 0x61, 0x6d, 0x65, // "displayName"
            0x6d, // text(13)
              0x4a, 0x6f, 0x68, 0x6e, 0x20, 0x50, 0x2e, 0x20, 0x53, 0x6d, 0x69, 0x74, 0x68, // "John P. Smith"
          0x04, // unsigned(4) - pubKeyCredParams
          0x82, // array(2)
            0xa2, // map(2)
              0x63, // text(3)
                0x61, 0x6c, 0x67, // "alg"
              0x26, // -7 (ES256)
              0x64, // text(4)
                0x74, 0x79, 0x70, 0x65, // "type"
              0x6a, // text(10)
                0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x2D, 0x6B, 0x65, 0x79, // "public-key"
              0xa2, // map(2)
              0x63, // text(3)
                0x61, 0x6c, 0x67, // "alg"
              0x39, 0x01, 0x00, // -257 (RS256)
              0x64, // text(4)
                0x74, 0x79, 0x70, 0x65, // "type"
              0x6a, // text(10)
                0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x2D, 0x6B, 0x65, 0x79, // "public-key"
          // TODO(MS): Options seem to be parsed differently than in the example here.
           0x07, // unsigned(7) - options
           0xa1, // map(1)
             0x62, // text(2)
               0x72, 0x6b, // "rk"
             0xf5, // primitive(21)
    ];

    pub const MAKE_CREDENTIALS_SAMPLE_REQUEST_CTAP1: [u8; 73] = [
        // CBOR Header
        0x0, // CLA
        0x1, // INS U2F_Register
        0x3, // P1 Flags
        0x0, // P2
        0x0, 0x0, 0x40, // Lc
        // NOTE: This has been taken from CTAP2.0 spec, but the clientDataHash has been replaced
        //       to be able to operate with known values for CollectedClientData (spec doesn't say
        //       what values led to the provided example hash)
        // clientDataHash:
        0x75, 0x35, 0x35, 0x7d, 0x49, 0x6e, 0x33, 0xc8, 0x18, 0x7f, 0xea, 0x8d, 0x11, // hash
        0x32, 0x64, 0xaa, 0xa4, 0x52, 0x3e, 0x13, 0x40, 0x14, 0x9f, 0xbe, 0x00, 0x3f, // hash
        0x10, 0x87, 0x54, 0xc3, 0x2d, 0x80, // hash
        // rpIdHash:
        0xA3, 0x79, 0xA6, 0xF6, 0xEE, 0xAF, 0xB9, 0xA5, 0x5E, 0x37, 0x8C, 0x11, 0x80, 0x34, 0xE2,
        0x75, 0x1E, 0x68, 0x2F, 0xAB, 0x9F, 0x2D, 0x30, 0xAB, 0x13, 0xD2, 0x12, 0x55, 0x86, 0xCE,
        0x19, 0x47, // ..
        // Le (Ne=65536):
        0x0, 0x0,
    ];

    pub const MAKE_CREDENTIALS_SAMPLE_RESPONSE_CTAP1: [u8; 792] = [
        0x05, // Reserved Byte (1 Byte)
        // User Public Key (65 Bytes)
        0x04, 0xE8, 0x76, 0x25, 0x89, 0x6E, 0xE4, 0xE4, 0x6D, 0xC0, 0x32, 0x76, 0x6E, 0x80, 0x87,
        0x96, 0x2F, 0x36, 0xDF, 0x9D, 0xFE, 0x8B, 0x56, 0x7F, 0x37, 0x63, 0x01, 0x5B, 0x19, 0x90,
        0xA6, 0x0E, 0x14, 0x27, 0xDE, 0x61, 0x2D, 0x66, 0x41, 0x8B, 0xDA, 0x19, 0x50, 0x58, 0x1E,
        0xBC, 0x5C, 0x8C, 0x1D, 0xAD, 0x71, 0x0C, 0xB1, 0x4C, 0x22, 0xF8, 0xC9, 0x70, 0x45, 0xF4,
        0x61, 0x2F, 0xB2, 0x0C, 0x91, // ...
        0x40, // Key Handle Length (1 Byte)
        // Key Handle (Key Handle Length Bytes)
        0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35, 0xEF, 0xAA,
        0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D, 0xA4, 0x85, 0x34, 0xC8,
        0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD,
        0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08,
        0xFE, 0x42, 0x00, 0x38, // ...
        // X.509 Cert (Variable length Cert)
        0x30, 0x82, 0x02, 0x4A, 0x30, 0x82, 0x01, 0x32, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x04,
        0x04, 0x6C, 0x88, 0x22, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01,
        0x01, 0x0B, 0x05, 0x00, 0x30, 0x2E, 0x31, 0x2C, 0x30, 0x2A, 0x06, 0x03, 0x55, 0x04, 0x03,
        0x13, 0x23, 0x59, 0x75, 0x62, 0x69, 0x63, 0x6F, 0x20, 0x55, 0x32, 0x46, 0x20, 0x52, 0x6F,
        0x6F, 0x74, 0x20, 0x43, 0x41, 0x20, 0x53, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x20, 0x34, 0x35,
        0x37, 0x32, 0x30, 0x30, 0x36, 0x33, 0x31, 0x30, 0x20, 0x17, 0x0D, 0x31, 0x34, 0x30, 0x38,
        0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5A, 0x18, 0x0F, 0x32, 0x30, 0x35, 0x30,
        0x30, 0x39, 0x30, 0x34, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5A, 0x30, 0x2C, 0x31, 0x2A,
        0x30, 0x28, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x21, 0x59, 0x75, 0x62, 0x69, 0x63, 0x6F,
        0x20, 0x55, 0x32, 0x46, 0x20, 0x45, 0x45, 0x20, 0x53, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x20,
        0x32, 0x34, 0x39, 0x31, 0x38, 0x32, 0x33, 0x32, 0x34, 0x37, 0x37, 0x30, 0x30, 0x59, 0x30,
        0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86, 0x48,
        0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x3C, 0xCA, 0xB9, 0x2C, 0xCB, 0x97,
        0x28, 0x7E, 0xE8, 0xE6, 0x39, 0x43, 0x7E, 0x21, 0xFC, 0xD6, 0xB6, 0xF1, 0x65, 0xB2, 0xD5,
        0xA3, 0xF3, 0xDB, 0x13, 0x1D, 0x31, 0xC1, 0x6B, 0x74, 0x2B, 0xB4, 0x76, 0xD8, 0xD1, 0xE9,
        0x90, 0x80, 0xEB, 0x54, 0x6C, 0x9B, 0xBD, 0xF5, 0x56, 0xE6, 0x21, 0x0F, 0xD4, 0x27, 0x85,
        0x89, 0x9E, 0x78, 0xCC, 0x58, 0x9E, 0xBE, 0x31, 0x0F, 0x6C, 0xDB, 0x9F, 0xF4, 0xA3, 0x3B,
        0x30, 0x39, 0x30, 0x22, 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0xC4, 0x0A, 0x02,
        0x04, 0x15, 0x31, 0x2E, 0x33, 0x2E, 0x36, 0x2E, 0x31, 0x2E, 0x34, 0x2E, 0x31, 0x2E, 0x34,
        0x31, 0x34, 0x38, 0x32, 0x2E, 0x31, 0x2E, 0x32, 0x30, 0x13, 0x06, 0x0B, 0x2B, 0x06, 0x01,
        0x04, 0x01, 0x82, 0xE5, 0x1C, 0x02, 0x01, 0x01, 0x04, 0x04, 0x03, 0x02, 0x04, 0x30, 0x30,
        0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B, 0x05, 0x00, 0x03,
        0x82, 0x01, 0x01, 0x00, 0x9F, 0x9B, 0x05, 0x22, 0x48, 0xBC, 0x4C, 0xF4, 0x2C, 0xC5, 0x99,
        0x1F, 0xCA, 0xAB, 0xAC, 0x9B, 0x65, 0x1B, 0xBE, 0x5B, 0xDC, 0xDC, 0x8E, 0xF0, 0xAD, 0x2C,
        0x1C, 0x1F, 0xFB, 0x36, 0xD1, 0x87, 0x15, 0xD4, 0x2E, 0x78, 0xB2, 0x49, 0x22, 0x4F, 0x92,
        0xC7, 0xE6, 0xE7, 0xA0, 0x5C, 0x49, 0xF0, 0xE7, 0xE4, 0xC8, 0x81, 0xBF, 0x2E, 0x94, 0xF4,
        0x5E, 0x4A, 0x21, 0x83, 0x3D, 0x74, 0x56, 0x85, 0x1D, 0x0F, 0x6C, 0x14, 0x5A, 0x29, 0x54,
        0x0C, 0x87, 0x4F, 0x30, 0x92, 0xC9, 0x34, 0xB4, 0x3D, 0x22, 0x2B, 0x89, 0x62, 0xC0, 0xF4,
        0x10, 0xCE, 0xF1, 0xDB, 0x75, 0x89, 0x2A, 0xF1, 0x16, 0xB4, 0x4A, 0x96, 0xF5, 0xD3, 0x5A,
        0xDE, 0xA3, 0x82, 0x2F, 0xC7, 0x14, 0x6F, 0x60, 0x04, 0x38, 0x5B, 0xCB, 0x69, 0xB6, 0x5C,
        0x99, 0xE7, 0xEB, 0x69, 0x19, 0x78, 0x67, 0x03, 0xC0, 0xD8, 0xCD, 0x41, 0xE8, 0xF7, 0x5C,
        0xCA, 0x44, 0xAA, 0x8A, 0xB7, 0x25, 0xAD, 0x8E, 0x79, 0x9F, 0xF3, 0xA8, 0x69, 0x6A, 0x6F,
        0x1B, 0x26, 0x56, 0xE6, 0x31, 0xB1, 0xE4, 0x01, 0x83, 0xC0, 0x8F, 0xDA, 0x53, 0xFA, 0x4A,
        0x8F, 0x85, 0xA0, 0x56, 0x93, 0x94, 0x4A, 0xE1, 0x79, 0xA1, 0x33, 0x9D, 0x00, 0x2D, 0x15,
        0xCA, 0xBD, 0x81, 0x00, 0x90, 0xEC, 0x72, 0x2E, 0xF5, 0xDE, 0xF9, 0x96, 0x5A, 0x37, 0x1D,
        0x41, 0x5D, 0x62, 0x4B, 0x68, 0xA2, 0x70, 0x7C, 0xAD, 0x97, 0xBC, 0xDD, 0x17, 0x85, 0xAF,
        0x97, 0xE2, 0x58, 0xF3, 0x3D, 0xF5, 0x6A, 0x03, 0x1A, 0xA0, 0x35, 0x6D, 0x8E, 0x8D, 0x5E,
        0xBC, 0xAD, 0xC7, 0x4E, 0x07, 0x16, 0x36, 0xC6, 0xB1, 0x10, 0xAC, 0xE5, 0xCC, 0x9B, 0x90,
        0xDF, 0xEA, 0xCA, 0xE6, 0x40, 0xFF, 0x1B, 0xB0, 0xF1, 0xFE, 0x5D, 0xB4, 0xEF, 0xF7, 0xA9,
        0x5F, 0x06, 0x07, 0x33, 0xF5, // ...
        // Signature (variable Length)
        0x30, 0x45, 0x02, 0x20, 0x32, 0x47, 0x79, 0xC6, 0x8F, 0x33, 0x80, 0x28, 0x8A, 0x11, 0x97,
        0xB6, 0x09, 0x5F, 0x7A, 0x6E, 0xB9, 0xB1, 0xB1, 0xC1, 0x27, 0xF6, 0x6A, 0xE1, 0x2A, 0x99,
        0xFE, 0x85, 0x32, 0xEC, 0x23, 0xB9, 0x02, 0x21, 0x00, 0xE3, 0x95, 0x16, 0xAC, 0x4D, 0x61,
        0xEE, 0x64, 0x04, 0x4D, 0x50, 0xB4, 0x15, 0xA6, 0xA4, 0xD4, 0xD8, 0x4B, 0xA6, 0xD8, 0x95,
        0xCB, 0x5A, 0xB7, 0xA1, 0xAA, 0x7D, 0x08, 0x1D, 0xE3, 0x41, 0xFA, // ...
    ];

    /// Test canonical encoding of MakeCredential requests.
    #[test]
    fn test_cbor_canonical() {
        const OPTION_BOOL_VALUES: [Option<bool>; 3] = [None, Some(false), Some(true)];
        let names = vec![None, Some(String::new()), Some(String::from("ABCD"))];

        let client_data_hash = CollectedClientData {
            webauthn_type: WebauthnType::Create,
            challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
            origin: String::from("example.com"),
            cross_origin: false,
            token_binding: None,
        }
        .hash()
        .expect("failed to serialize client data");

        for rp_name in &names {
            for user_name in &names {
                for display_name in &names {
                    for resident_key in OPTION_BOOL_VALUES {
                        for user_verification in OPTION_BOOL_VALUES {
                            let req = MakeCredentials::new(
                                client_data_hash.clone(),
                                RelyingParty {
                                    id: String::from("example.com"),
                                    name: rp_name.clone(),
                                },
                                Some(PublicKeyCredentialUserEntity {
                                    id: vec![0x01; 32],
                                    name: user_name.clone(),
                                    display_name: display_name.clone(),
                                }),
                                vec![PublicKeyCredentialParameters {
                                    alg: COSEAlgorithm::ES256,
                                }],
                                Vec::new(),
                                MakeCredentialsOptions {
                                    resident_key,
                                    user_verification,
                                },
                                Default::default(),
                            );
                            assert_canonical_cbor_encoding(&req);
                        }
                    }
                }
            }
        }
    }
}
