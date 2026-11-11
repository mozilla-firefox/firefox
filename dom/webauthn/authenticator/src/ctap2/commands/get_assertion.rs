/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use super::get_info::AuthenticatorInfo;
use super::large_blobs::LargeBlobArrayElement;
use super::{
    Command, CommandError, CtapResponse, PinUvAuthCommand, PinUvAuthResult, RequestCtap1,
    RequestCtap2, Retryable, StatusCode,
};
use crate::consts::{
    PARAMETER_SIZE, U2F_AUTHENTICATE, U2F_DONT_ENFORCE_USER_PRESENCE_AND_SIGN,
    U2F_REQUEST_USER_PRESENCE,
};
use crate::crypto::{COSEKey, CryptoError, PinUvAuthParam, PinUvAuthToken, SharedSecret};
use crate::ctap2::attestation::{AuthenticatorData, AuthenticatorDataFlags, HmacSecretResponse};
use crate::ctap2::client_data::ClientDataHash;
use crate::ctap2::commands::get_next_assertion::GetNextAssertion;
use crate::ctap2::commands::make_credentials::UserVerification;
use crate::ctap2::server::{
    AuthenticationExtensionsClientInputs, AuthenticationExtensionsClientOutputs,
    AuthenticationExtensionsPRFInputs, AuthenticationExtensionsPRFOutputs, AuthenticatorAttachment,
    AuthenticatorExtensionsCredBlob, PublicKeyCredentialDescriptor, PublicKeyCredentialUserEntity,
    RelyingParty, RpIdHash, UserVerificationRequirement,
};
use crate::ctap2::utils::{read_be_u32, read_byte};
use crate::errors::AuthenticatorError;
use crate::transport::errors::{ApduErrorStatus, HIDError};
use crate::transport::{FidoDevice, VirtualFidoDevice};
use crate::u2ftypes::CTAP1RequestAPDU;
use serde::{
    de::{Error as DesError, MapAccess, Visitor},
    ser::Error as SerError,
    Deserialize, Deserializer, Serialize, Serializer,
};
use serde_bytes::ByteBuf;
use serde_cbor::{de::from_slice, ser, Value};
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fmt;
use std::io::Cursor;

#[derive(Clone, Copy, Debug, Serialize)]
#[cfg_attr(test, derive(Deserialize))]
pub struct GetAssertionOptions {
    #[serde(rename = "up", skip_serializing_if = "Option::is_none")]
    pub user_presence: Option<bool>,
    #[serde(rename = "uv", skip_serializing_if = "Option::is_none")]
    pub user_verification: Option<bool>,
}

impl Default for GetAssertionOptions {
    fn default() -> Self {
        Self {
            user_presence: Some(true),
            user_verification: None,
        }
    }
}

impl GetAssertionOptions {
    pub(crate) fn has_some(&self) -> bool {
        self.user_presence.is_some() || self.user_verification.is_some()
    }
}

impl UserVerification for GetAssertionOptions {
    fn ask_user_verification(&self) -> bool {
        self.user_verification.unwrap_or(false)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct CalculatedHmacSecretExtension {
    pub public_key: COSEKey,
    pub salt_enc: Vec<u8>,
    pub salt_auth: Vec<u8>,
}

/// Wrapper type recording whether the hmac-secret input originally came from the hmacGetSecret or the prf client extension input.
#[derive(Debug, Clone, PartialEq)]
pub enum HmacGetSecretOrPrf {
    /// hmac-secret inputs set by the hmacGetSecret client extension input.
    HmacGetSecret(HmacSecretExtension),
    /// hmac-secret input is to be calculated from PRF inputs, but we haven't yet identified which eval or evalByCredential entry to use.
    PrfUninitialized(AuthenticationExtensionsPRFInputs),
    /// prf client input with no eval or matching evalByCredential entry.
    PrfUnmatched,
    /// hmac-secret inputs set by the prf client extension input.
    Prf(HmacSecretExtension),
}

impl HmacGetSecretOrPrf {
    fn skip_serializing(value: &Option<Self>) -> bool {
        matches!(value, None | Some(Self::PrfUnmatched))
    }

    /// Calculate the appropriate hmac-secret or PRF salt inputs from the given inputs.
    ///
    /// - If this is a `HmacGetSecret` instance,
    ///   this returns a new `HmacGetSecret` instance with `calculated_hmac` set, paired with [None].
    /// - If this is a `PrfUninitialized` instance,
    ///   this attempts to select a PRF input to calculate salts from.
    ///   If an input is found, this returns a `Prf` instance with `calculated_hmac` set.
    ///   If the selected input came from `eval_by_credential`,
    ///   then this is paired with a [Some] referencing the matching element of `allow_credentials`.
    ///   If the selected input was `eval`, then this is paired with [None].
    ///   If no input is found, this returns `PrfUnmatched` and [None].
    /// - If this is a `Prf` or `PrfUnmatched` instance, this panics.
    ///
    /// If the [Option] return value is [Some], the caller SHOULD set `allowCredentials`
    /// to contain only that [PublicKeyCredentialDescriptor] value.
    ///
    /// # Panics
    /// If this is a `Prf` or `PrfUnmatched` instance.
    pub fn calculate<'allow_cred>(
        self,
        secret: &SharedSecret,
        allow_credentials: &'allow_cred [PublicKeyCredentialDescriptor],
        puat: Option<&PinUvAuthToken>,
    ) -> Result<(Self, Option<&'allow_cred PublicKeyCredentialDescriptor>), CryptoError> {
        Ok(match self {
            Self::HmacGetSecret(mut extension) => {
                extension.calculate(secret, puat)?;
                (Self::HmacGetSecret(extension), None)
            }

            Self::PrfUninitialized(prf) => match prf.calculate(secret, allow_credentials, puat)? {
                Some((hmac_secret, selected_credential)) => {
                    (Self::Prf(hmac_secret), selected_credential)
                }
                None => (Self::PrfUnmatched, None),
            },

            Self::Prf(_) | Self::PrfUnmatched => {
                unreachable!("hmac-secret inputs from PRF already initialized")
            }
        })
    }
}

impl Serialize for HmacGetSecretOrPrf {
    fn serialize<S>(&self, s: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Self::HmacGetSecret(ext) => ext.serialize(s),
            Self::PrfUninitialized(_) => Err(serde::ser::Error::custom(
                "PrfUninitialized must be replaced with Prf or PrfUnmatched before serializing",
            )),
            Self::PrfUnmatched => unreachable!("PrfUnmatched serialization should be skipped"),
            Self::Prf(ext) => ext.serialize(s),
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct HmacSecretExtension {
    pub salt1: Vec<u8>,
    pub salt2: Option<Vec<u8>>,
    calculated_hmac: Option<CalculatedHmacSecretExtension>,
    pin_protocol: Option<u64>,
}

impl HmacSecretExtension {
    pub fn new(salt1: Vec<u8>, salt2: Option<Vec<u8>>) -> Self {
        HmacSecretExtension {
            salt1,
            salt2,
            calculated_hmac: None,
            pin_protocol: None,
        }
    }

    #[cfg(test)]
    pub fn new_test(
        salt1: Vec<u8>,
        salt2: Option<Vec<u8>>,
        calculated_hmac: CalculatedHmacSecretExtension,
        pin_protocol: Option<u64>,
    ) -> Self {
        HmacSecretExtension {
            salt1,
            salt2,
            calculated_hmac: Some(calculated_hmac),
            pin_protocol,
        }
    }

    /// Calculate inputs for the `hmac-secret` extension.
    /// See "authenticatorGetAssertion additional behaviors"
    /// in https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html#sctn-hmac-secret-extension
    pub fn calculate(
        &mut self,
        secret: &SharedSecret,
        puat: Option<&PinUvAuthToken>,
    ) -> Result<(), CryptoError> {
        let salt_enc = match (
            <[u8; 32]>::try_from(self.salt1.as_slice()),
            self.salt2.as_deref().map(<[u8; 32]>::try_from),
        ) {
            (Ok(salt1), None) => secret.encrypt(&salt1),
            (Ok(salt1), Some(Ok(salt2))) => secret.encrypt(&[salt1, salt2].concat()),
            (Err(_), _) | (_, Some(Err(_))) => {
                debug!(
                    "Invalid hmac-secret salt length(s): salt1: {}, salt2: {:?} (expected 32 and 32|None)",
                    self.salt1.len(),
                    self.salt2.as_ref().map(Vec::len)
                );
                Err(CryptoError::WrongSaltLength)
            }
        }?;
        let salt_auth = secret.authenticate(&salt_enc)?;
        let public_key = secret.client_input().clone();
        self.calculated_hmac = Some(CalculatedHmacSecretExtension {
            public_key,
            salt_enc,
            salt_auth,
        });

        // CTAP2.1 platforms MUST include this parameter if the value of pinUvAuthProtocol is not 1.
        self.pin_protocol = puat
            .map(|puat| puat.pin_protocol.id())
            .filter(|id| *id != 1);

        Ok(())
    }
}

impl Serialize for HmacSecretExtension {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        if let Some(calc) = &self.calculated_hmac {
            serialize_map_optional! {
                serializer,
                &1 => Some(&calc.public_key),
                &2 => Some(serde_bytes::Bytes::new(&calc.salt_enc)),
                &3 => Some(serde_bytes::Bytes::new(&calc.salt_auth)),
                &4 => &self.pin_protocol,
            }
        } else {
            Err(SerError::custom(
                "hmac secret has not been calculated before being serialized",
            ))
        }
    }
}

#[derive(Debug, Default, Clone, Serialize)]
pub struct GetAssertionExtensions {
    #[serde(skip_serializing)]
    pub app_id: Option<String>,
    #[serde(
        rename = "hmac-secret",
        skip_serializing_if = "HmacGetSecretOrPrf::skip_serializing"
    )]
    pub hmac_secret: Option<HmacGetSecretOrPrf>,
    #[serde(rename = "credBlob", skip_serializing_if = "Option::is_none")]
    pub cred_blob: Option<bool>,
    #[serde(rename = "largeBlobKey", skip_serializing_if = "Option::is_none")]
    pub large_blob_key: Option<bool>,
    #[serde(rename = "thirdPartyPayment", skip_serializing_if = "Option::is_none")]
    pub third_party_payment: Option<bool>,
}

impl From<AuthenticationExtensionsClientInputs> for GetAssertionExtensions {
    fn from(input: AuthenticationExtensionsClientInputs) -> Self {
        let prf = input.prf;
        Self {
            app_id: input.app_id,
            hmac_secret: input
                .hmac_get_secret
                .map(|hmac_secret| {
                    HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                        hmac_secret.salt1.into(),
                        hmac_secret.salt2.map(|salt2| salt2.into()),
                    ))
                })
                .or_else(
                    || prf.map(HmacGetSecretOrPrf::PrfUninitialized), // Cannot calculate hmac-secret inputs here because we don't yet know which eval or evalByCredential entry to use
                ),
            cred_blob: match input.cred_blob {
                Some(AuthenticatorExtensionsCredBlob::AsBool(x)) => Some(x),
                _ => None,
            },
            large_blob_key: input.large_blob_key,
            third_party_payment: input.third_party_payment,
        }
    }
}

impl GetAssertionExtensions {
    fn has_content(&self) -> bool {
        self.hmac_secret.is_some()
            || self.cred_blob.is_some()
            || self.large_blob_key.is_some()
            || self.third_party_payment.is_some()
    }
}

#[derive(Debug, Clone)]
pub struct GetAssertion {
    pub client_data_hash: ClientDataHash,
    pub rp: RelyingParty,
    pub allow_list: Vec<PublicKeyCredentialDescriptor>,

    // https://www.w3.org/TR/webauthn/#client-extension-input
    // The client extension input, which is a value that can be encoded in JSON,
    // is passed from the WebAuthn Relying Party to the client in the get() or
    // create() call, while the CBOR authenticator extension input is passed
    // from the client to the authenticator for authenticator extensions during
    // the processing of these calls.
    pub extensions: GetAssertionExtensions,
    pub options: GetAssertionOptions,
    pub pin_uv_auth_param: Option<PinUvAuthParam>,

    // CTAP 2.2:
    pub enterprise_attestation: Option<u64>,
    pub attestation_formats_preference: Option<Vec<String>>,
}

impl GetAssertion {
    pub fn new(
        client_data_hash: ClientDataHash,
        rp: RelyingParty,
        allow_list: Vec<PublicKeyCredentialDescriptor>,
        options: GetAssertionOptions,
        extensions: GetAssertionExtensions,
    ) -> Self {
        Self {
            client_data_hash,
            rp,
            allow_list,
            extensions,
            options,
            pin_uv_auth_param: None,
            enterprise_attestation: None,
            attestation_formats_preference: None,
        }
    }

    pub fn process_hmac_secret_and_prf_extension(
        mut self,
        shared_secret: Option<(&SharedSecret, &PinUvAuthResult)>,
    ) -> Result<Self, AuthenticatorError> {
        let (new_hmac_secret, new_allow_list) = self
            .extensions
            .hmac_secret
            .take()
            .and_then(|hmac_get_secret_or_prf| {
                if let Some((secret, pin_uv_auth_result)) = shared_secret {
                    Some(hmac_get_secret_or_prf.calculate(
                        secret,
                        &self.allow_list,
                        pin_uv_auth_result.get_pin_uv_auth_token().as_ref(),
                    ))
                } else {
                    debug!(
                        "Shared secret not available - will not send hmac-secret extension input: {:?}",
                        hmac_get_secret_or_prf
                    );
                    match hmac_get_secret_or_prf {
                        HmacGetSecretOrPrf::HmacGetSecret(_) => None,
                        HmacGetSecretOrPrf::PrfUninitialized(_)
                        | HmacGetSecretOrPrf::PrfUnmatched
                        | HmacGetSecretOrPrf::Prf(_) => {
                            Some(Ok((HmacGetSecretOrPrf::PrfUnmatched, None)))
                        }
                    }
                }
            })
            .transpose()
            .map_err(|err| match err {
                CryptoError::WrongSaltLength => AuthenticatorError::InvalidRelyingPartyInput,
                e => e.into(),
            })?
            .map(|(nhs, nal)| (Some(nhs), nal))
            .unwrap_or((None, None));

        (self.extensions.hmac_secret, self.allow_list) = (
            new_hmac_secret,
            new_allow_list
                .map(|selected_credential| vec![selected_credential.clone()])
                .unwrap_or(self.allow_list),
        );

        Ok(self)
    }

    pub fn finalize_result<Dev: FidoDevice>(&self, dev: &Dev, result: &mut GetAssertionResult) {
        result.attachment = match dev.get_authenticator_info() {
            Some(info) if info.options.platform_device => AuthenticatorAttachment::Platform,
            Some(_) => AuthenticatorAttachment::CrossPlatform,
            None => AuthenticatorAttachment::Unknown,
        };

        // Handle extensions whose outputs are not encoded in the authenticator data.
        // 1. appId
        if let Some(app_id) = &self.extensions.app_id {
            result.extensions.app_id =
                Some(result.assertion.auth_data.rp_id_hash == RelyingParty::from(app_id).hash());
        }

        // 2. prf
        //      If the prf extension was requested and hmac-secret returned secrets,
        //      we need to decrypt and output them as prf client outputs.
        match self.extensions.hmac_secret {
            Some(HmacGetSecretOrPrf::HmacGetSecret(_)) => {
                result.extensions.hmac_get_secret =
                    if let Some(hmac_response @ HmacSecretResponse::Secret(_)) =
                        &result.assertion.auth_data.extensions.hmac_secret
                    {
                        dev.get_shared_secret()
                            .and_then(|shared_secret| hmac_response.decrypt_secrets(shared_secret))
                            .and_then(|result| match result {
                                Ok(ok) => Some(ok),
                                Err(err) => {
                                    debug!("Failed to decrypt hmac-secret response: {:?}", err);
                                    None
                                }
                            })
                    } else {
                        None
                    };
            }
            Some(HmacGetSecretOrPrf::PrfUninitialized(_)) => {
                unreachable!(
                    "Reached GetAssertion.finalize_result without replacing PrfUninitialized instance with Prf"
                )
            }
            Some(HmacGetSecretOrPrf::PrfUnmatched) => {
                result.extensions.prf = Some(AuthenticationExtensionsPRFOutputs {
                    enabled: None,
                    results: None,
                });
            }
            Some(HmacGetSecretOrPrf::Prf(_)) => {
                result.extensions.prf = Some(AuthenticationExtensionsPRFOutputs {
                    enabled: None,
                    results: if let Some(hmac_response @ HmacSecretResponse::Secret(_)) =
                        &result.assertion.auth_data.extensions.hmac_secret
                    {
                        dev.get_shared_secret()
                            .and_then(|shared_secret| hmac_response.decrypt_secrets(shared_secret))
                            .and_then(|result| match result {
                                Ok(ok) => Some(ok),
                                Err(err) => {
                                    debug!("Failed to decrypt hmac-secret response: {:?}", err);
                                    None
                                }
                            })
                            .map(|outputs| outputs.into())
                    } else {
                        None
                    },
                });
            }
            None => {}
        }

        // 3. credBlob
        //      The extension returns a flag in the authenticator data which we need to mirror as a
        //      client output.
        result.extensions.cred_blob = result.assertion.auth_data.extensions.cred_blob.clone();
    }
}

impl PinUvAuthCommand for GetAssertion {
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
        let supports_uv = info.options.user_verification == Some(true);
        let pin_configured = info.options.client_pin == Some(true);
        let device_protected = supports_uv || pin_configured;
        let uv_discouraged = uv_req == UserVerificationRequirement::Discouraged;
        let always_uv = info.options.always_uv == Some(true);

        !always_uv && (!device_protected || uv_discouraged)
    }

    fn get_pin_uv_auth_param(&self) -> Option<&PinUvAuthParam> {
        self.pin_uv_auth_param.as_ref()
    }

    fn hmac_requested(&self) -> bool {
        self.extensions.hmac_secret.is_some()
    }
}

impl Serialize for GetAssertion {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serialize_map_optional! {
            serializer,
            &1 => Some(&self.rp.id),
            &2 => Some(&self.client_data_hash),
            &3 => (!&self.allow_list.is_empty()).then_some(&self.allow_list),
            &4 => self.extensions.has_content().then_some(&self.extensions),
            &5 => self.options.has_some().then_some(&self.options),
            &6 => &self.pin_uv_auth_param,
            &7 => self.pin_uv_auth_param.as_ref().map(|p| p.pin_protocol.id()),
            &8 => &self.enterprise_attestation,
            &9 => &self.attestation_formats_preference,
        }
    }
}

type GetAssertionOutput = Vec<GetAssertionResult>;
impl CtapResponse for GetAssertionOutput {}

impl RequestCtap1 for GetAssertion {
    type Output = Vec<GetAssertionResult>;
    type AdditionalInfo = PublicKeyCredentialDescriptor;

    fn ctap1_format(&self) -> Result<(Vec<u8>, Self::AdditionalInfo), HIDError> {
        // Pre-flighting should reduce the list to exactly one entry
        let key_handle = match &self.allow_list[..] {
            [key_handle] => key_handle,
            [] => {
                return Err(HIDError::Command(CommandError::StatusCode(
                    StatusCode::NoCredentials,
                    None,
                )));
            }
            _ => {
                return Err(HIDError::UnsupportedCommand);
            }
        };

        debug!("sending key_handle = {:?}", key_handle);

        let flags = if self.options.user_presence.unwrap_or(true) {
            U2F_REQUEST_USER_PRESENCE
        } else {
            U2F_DONT_ENFORCE_USER_PRESENCE_AND_SIGN
        };
        let mut auth_data =
            Vec::with_capacity(2 * PARAMETER_SIZE + 1 /* key_handle_len */ + key_handle.id.len());

        auth_data.extend_from_slice(self.client_data_hash.as_ref());
        auth_data.extend_from_slice(self.rp.hash().as_ref());
        auth_data.extend_from_slice(&[key_handle.id.len() as u8]);
        auth_data.extend_from_slice(key_handle.id.as_ref());

        let cmd = U2F_AUTHENTICATE;
        let apdu = CTAP1RequestAPDU::serialize(cmd, flags, &auth_data)?;
        Ok((apdu, key_handle.clone()))
    }

    fn handle_response_ctap1<Dev: FidoDevice>(
        &self,
        dev: &mut Dev,
        status: Result<(), ApduErrorStatus>,
        input: &[u8],
        add_info: &PublicKeyCredentialDescriptor,
    ) -> Result<Self::Output, Retryable<HIDError>> {
        if Err(ApduErrorStatus::ConditionsNotSatisfied) == status {
            return Err(Retryable::Retry);
        }
        if let Err(err) = status {
            return Err(Retryable::Error(HIDError::ApduStatus(err)));
        }

        let mut result = GetAssertionResult::from_ctap1(input, &self.rp.hash(), add_info)
            .map_err(|e| Retryable::Error(HIDError::Command(e)))?;
        self.finalize_result(dev, &mut result);
        // Although there's only one result, we return a vector for consistency with CTAP2.
        Ok(vec![result])
    }

    fn send_to_virtual_device<Dev: VirtualFidoDevice>(
        &self,
        dev: &mut Dev,
    ) -> Result<Self::Output, HIDError> {
        let mut results = dev.get_assertion(self)?;
        for result in results.iter_mut() {
            self.finalize_result(dev, result);
        }
        Ok(results)
    }
}

impl RequestCtap2 for GetAssertion {
    type Output = Vec<GetAssertionResult>;

    fn command(&self) -> Command {
        Command::GetAssertion
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
            return Err(CommandError::InputTooSmall.into());
        }

        let status: StatusCode = input[0].into();
        debug!(
            "response status code: {:?}, rest: {:?}",
            status,
            &input[1..]
        );
        if input.len() == 1 {
            if status.is_ok() {
                return Err(CommandError::InputTooSmall.into());
            }
            return Err(CommandError::StatusCode(status, None).into());
        }

        if status.is_ok() {
            let assertion: GetAssertionResponse =
                from_slice(&input[1..]).map_err(CommandError::Deserializing)?;
            let number_of_credentials = assertion.number_of_credentials.unwrap_or(1);
            let user_selected = assertion.user_selected;
            let large_blob_key = assertion.large_blob_key.clone();
            let mut results = Vec::with_capacity(number_of_credentials);
            results.push(GetAssertionResult {
                assertion: assertion.into(),
                attachment: AuthenticatorAttachment::Unknown,
                extensions: Default::default(),
                user_selected,
                large_blob_key,
                large_blob_array: None,
            });

            let msg = GetNextAssertion;
            // We already have one, so skipping 0
            for _ in 1..number_of_credentials {
                let assertion = dev.send_cbor(&msg, None)?;
                let user_selected = assertion.user_selected;
                let large_blob_key = assertion.large_blob_key.clone();
                results.push(GetAssertionResult {
                    assertion: assertion.into(),
                    attachment: AuthenticatorAttachment::Unknown,
                    extensions: Default::default(),
                    user_selected,
                    large_blob_key,
                    large_blob_array: None,
                });
            }

            for result in results.iter_mut() {
                self.finalize_result(dev, result);
            }
            Ok(results)
        } else {
            let data: Value = from_slice(&input[1..]).map_err(CommandError::Deserializing)?;
            Err(CommandError::StatusCode(status, Some(data)).into())
        }
    }

    fn send_to_virtual_device<Dev: VirtualFidoDevice>(
        &self,
        dev: &mut Dev,
    ) -> Result<Self::Output, HIDError> {
        let mut results = dev.get_assertion(self)?;
        for result in results.iter_mut() {
            self.finalize_result(dev, result);
        }
        Ok(results)
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct Assertion {
    pub credentials: Option<PublicKeyCredentialDescriptor>, /* Was optional in CTAP2.0, is
                                                             * mandatory in CTAP2.1 */
    pub auth_data: AuthenticatorData,
    pub signature: Vec<u8>,
    pub user: Option<PublicKeyCredentialUserEntity>,
}

impl From<GetAssertionResponse> for Assertion {
    fn from(r: GetAssertionResponse) -> Self {
        Assertion {
            credentials: r.credentials,
            auth_data: r.auth_data,
            signature: r.signature,
            user: r.user,
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct GetAssertionResult {
    pub assertion: Assertion,
    pub attachment: AuthenticatorAttachment,
    pub extensions: AuthenticationExtensionsClientOutputs,
    pub user_selected: Option<bool>,
    pub large_blob_key: Option<Vec<u8>>,
    pub large_blob_array: Option<Vec<LargeBlobArrayElement>>,
}

impl GetAssertionResult {
    pub fn from_ctap1(
        input: &[u8],
        rp_id_hash: &RpIdHash,
        key_handle: &PublicKeyCredentialDescriptor,
    ) -> Result<GetAssertionResult, CommandError> {
        let mut data = Cursor::new(input);
        let user_presence = read_byte(&mut data).map_err(CommandError::Deserializing)?;
        let counter = read_be_u32(&mut data).map_err(CommandError::Deserializing)?;
        // Remaining data is signature (Note: `data.remaining_slice()` is not yet stabilized)
        let signature = Vec::from(&data.get_ref()[data.position() as usize..]);

        // Step 5 of Section 10.3 of CTAP2.1: "Copy bits 0 (the UP bit) and bit 1 from the
        // CTAP2/U2F response user presence byte to bits 0 and 1 of the CTAP2 flags, respectively.
        // Set all other bits of flags to zero."
        let flag_mask = AuthenticatorDataFlags::USER_PRESENT | AuthenticatorDataFlags::RESERVED_1;
        let flags = flag_mask & AuthenticatorDataFlags::from_bits_truncate(user_presence);
        let auth_data = AuthenticatorData {
            rp_id_hash: rp_id_hash.clone(),
            flags,
            counter,
            credential_data: None,
            extensions: Default::default(),
        };
        let assertion = Assertion {
            credentials: Some(key_handle.clone()),
            signature,
            user: None,
            auth_data,
        };

        Ok(GetAssertionResult {
            assertion,
            attachment: AuthenticatorAttachment::Unknown,
            extensions: Default::default(),
            user_selected: None,
            large_blob_key: None,
            large_blob_array: None,
        })
    }
}

#[derive(Debug, PartialEq)]
pub struct GetAssertionResponse {
    pub credentials: Option<PublicKeyCredentialDescriptor>,
    pub auth_data: AuthenticatorData,
    pub signature: Vec<u8>,
    pub user: Option<PublicKeyCredentialUserEntity>,
    pub number_of_credentials: Option<usize>,
    pub user_selected: Option<bool>,
    pub large_blob_key: Option<Vec<u8>>,
    pub unsigned_extension_outputs: Option<HashMap<String, serde_cbor::Value>>,
    pub ep_attestation: Option<bool>,
    pub att_stmt: Option<HashMap<String, serde_cbor::Value>>,
}

impl CtapResponse for GetAssertionResponse {}

impl<'de> Deserialize<'de> for GetAssertionResponse {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct GetAssertionResponseVisitor;

        impl<'de> Visitor<'de> for GetAssertionResponseVisitor {
            type Value = GetAssertionResponse;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a byte array")
            }

            fn visit_map<M>(self, mut map: M) -> Result<Self::Value, M::Error>
            where
                M: MapAccess<'de>,
            {
                let mut credentials = None;
                let mut auth_data = None;
                let mut signature = None;
                let mut user = None;
                let mut number_of_credentials = None;
                let mut user_selected = None;
                let mut large_blob_key = None;
                let mut unsigned_extension_outputs = None;
                let mut ep_attestation = None;
                let mut att_stmt = None;

                while let Some(key) = map.next_key()? {
                    match key {
                        0x01 => {
                            if credentials.is_some() {
                                return Err(M::Error::duplicate_field("credentials"));
                            }
                            credentials = Some(map.next_value()?);
                        }
                        0x02 => {
                            if auth_data.is_some() {
                                return Err(M::Error::duplicate_field("auth_data"));
                            }
                            auth_data = Some(map.next_value()?);
                        }
                        0x03 => {
                            if signature.is_some() {
                                return Err(M::Error::duplicate_field("signature"));
                            }
                            let signature_bytes: ByteBuf = map.next_value()?;
                            signature = Some(signature_bytes.into_vec());
                        }
                        0x04 => {
                            if user.is_some() {
                                return Err(M::Error::duplicate_field("user"));
                            }
                            user = map.next_value()?;
                        }
                        0x05 => {
                            if number_of_credentials.is_some() {
                                return Err(M::Error::duplicate_field("number_of_credentials"));
                            }
                            number_of_credentials = Some(map.next_value()?);
                        }
                        0x06 => {
                            if user_selected.is_some() {
                                return Err(M::Error::duplicate_field("user_selected"));
                            }
                            user_selected = Some(map.next_value()?);
                        }
                        0x07 => {
                            if large_blob_key.is_some() {
                                return Err(M::Error::duplicate_field("large_blob_key"));
                            }
                            let large_blob_key_bytes: ByteBuf = map.next_value()?;
                            large_blob_key = Some(large_blob_key_bytes.into_vec());
                        }
                        0x08 => {
                            if unsigned_extension_outputs.is_some() {
                                return Err(M::Error::duplicate_field(
                                    "unsigned_extension_outputs",
                                ));
                            }
                            unsigned_extension_outputs = Some(map.next_value()?);
                        }
                        0x09 => {
                            if ep_attestation.is_some() {
                                return Err(M::Error::duplicate_field("ep_attestation"));
                            }
                            ep_attestation = Some(map.next_value()?);
                        }
                        0x0A => {
                            if att_stmt.is_some() {
                                return Err(M::Error::duplicate_field("att_stmt"));
                            }
                            att_stmt = Some(map.next_value()?);
                        }
                        k => return Err(M::Error::custom(format!("unexpected key: {k:?}"))),
                    }
                }

                let auth_data = auth_data.ok_or_else(|| M::Error::missing_field("auth_data"))?;
                let signature = signature.ok_or_else(|| M::Error::missing_field("signature"))?;

                Ok(GetAssertionResponse {
                    credentials,
                    auth_data,
                    signature,
                    user,
                    number_of_credentials,
                    user_selected,
                    large_blob_key,
                    unsigned_extension_outputs,
                    ep_attestation,
                    att_stmt,
                })
            }
        }

        deserializer.deserialize_bytes(GetAssertionResponseVisitor)
    }
}

#[cfg(test)]
pub mod test {

    use super::{
        Assertion, CommandError, GetAssertion, GetAssertionOptions, GetAssertionResult, HIDError,
        StatusCode,
    };
    use crate::consts::{
        Capability, HIDCmd, SW_CONDITIONS_NOT_SATISFIED, SW_NO_ERROR, U2F_CHECK_IS_REGISTERED,
        U2F_REQUEST_USER_PRESENCE,
    };
    use crate::crypto::{COSEAlgorithm, COSEEC2Key, COSEKey, COSEKeyType, Curve, PinUvAuthParam};
    use crate::ctap2::attestation::{AAGuid, AuthenticatorData, AuthenticatorDataFlags};
    use crate::ctap2::client_data::{
        Challenge, ClientDataHash, CollectedClientData, TokenBinding, WebauthnType,
    };
    use crate::ctap2::commands::client_pin::PinUvAuthTokenPermission;
    use crate::ctap2::commands::get_assertion::{
        CalculatedHmacSecretExtension, GetAssertionExtensions, HmacGetSecretOrPrf,
        HmacSecretExtension,
    };
    use crate::ctap2::commands::get_info::tests::AAGUID_RAW;
    use crate::ctap2::commands::get_info::{
        AuthenticatorInfo, AuthenticatorOptions, AuthenticatorVersion,
    };
    use crate::ctap2::commands::{assert_canonical_cbor_encoding, RequestCtap1, RequestCtap2};
    use crate::ctap2::preflight::{
        do_credential_list_filtering_ctap1, do_credential_list_filtering_ctap2,
    };
    use crate::ctap2::server::{
        AuthenticationExtensionsPRFInputs, AuthenticatorAttachment, PublicKeyCredentialDescriptor,
        PublicKeyCredentialUserEntity, RelyingParty, RpIdHash, Transport,
    };
    use crate::transport::device_selector::Device;
    use crate::transport::hid::HIDDevice;
    use crate::transport::{CtapVersionSupport, FidoDevice, FidoDeviceIO, FidoProtocol};
    use crate::u2ftypes::U2FDeviceInfo;
    use rand::{thread_rng, RngCore};
    use std::sync::mpsc::channel;

    #[test]
    fn test_get_assertion_ctap2() {
        let client_data = CollectedClientData {
            webauthn_type: WebauthnType::Create,
            challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
            origin: String::from("example.com"),
            cross_origin: false,
            token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
        };
        let assertion = GetAssertion::new(
            client_data.hash().expect("failed to serialize client data"),
            RelyingParty::from("example.com"),
            vec![PublicKeyCredentialDescriptor {
                id: vec![
                    0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35,
                    0xEF, 0xAA, 0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D,
                    0xA4, 0x85, 0x34, 0xC8, 0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5,
                    0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD, 0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5,
                    0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08, 0xFE, 0x42, 0x00, 0x38,
                ],
                transports: vec![Transport::USB],
            }],
            GetAssertionOptions {
                user_presence: Some(true),
                user_verification: None,
            },
            Default::default(),
        );
        assert_canonical_cbor_encoding(&assertion);
        let mut device = Device::new_pre_inited(
            "commands/get_assertion",
            Capability::CBOR | Capability::NMSG | Capability::WINK,
        );
        let cid = *device.get_cid();

        let mut msg = cid.to_vec();
        msg.extend(vec![HIDCmd::Cbor.into(), 0x00, 0x90]);
        msg.extend(vec![0x2]); // u2f command
        msg.extend(vec![
            0xa4, // map(4)
            0x1,  // rpid
            0x6b, // text(11)
            101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, // example.com
            0x2, // clientDataHash
            0x58, 0x20, //bytes(32)
            0x75, 0x35, 0x35, 0x7d, 0x49, 0x6e, 0x33, 0xc8, 0x18, 0x7f, 0xea, 0x8d, 0x11, 0x32,
            0x64, 0xaa, 0xa4, 0x52, 0x3e, 0x13, 0x40, 0x14, 0x9f, 0xbe, 0x00, 0x3f, 0x10, 0x87,
            0x54, 0xc3, 0x2d, 0x80, // hash
            0x3,  //allowList
            0x81, // array(1)
            0xa2, // map(2)
            0x62, // text(2)
            0x69, 0x64, // id
            0x58, // bytes(
        ]);
        device.add_write(&msg, 0);

        msg = cid.to_vec();
        msg.extend([0x0]); //SEQ
        msg.extend([0x40]); // 64)
        msg.extend(&assertion.allow_list[0].id[..58]);
        device.add_write(&msg, 0);

        msg = cid.to_vec();
        msg.extend([0x1]); //SEQ
        msg.extend(&assertion.allow_list[0].id[58..64]);
        msg.extend(vec![
            0x64, // text(4),
            0x74, 0x79, 0x70, 0x65, // type
            0x6a, // text(10)
            0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x2D, 0x6B, 0x65, 0x79, // public-key
            0x5,  // options
            0xa1, // map(1)
            0x62, // text(2)
            0x75, 0x70, // up
            0xf5, // true
        ]);
        device.add_write(&msg, 0);

        // fido response
        let mut msg = cid.to_vec();
        msg.extend([HIDCmd::Cbor.into(), 0x1, 0x2a]); // cmd + bcnt
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[..57]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x0]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[57..116]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x1]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[116..175]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x2]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[175..234]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x3]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[234..293]);
        device.add_read(&msg, 0);
        let mut msg = cid.to_vec();
        msg.extend([0x4]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[293..]);
        device.add_read(&msg, 0);

        // Check if response is correct
        let expected_auth_data = AuthenticatorData {
            rp_id_hash: RpIdHash([
                0x62, 0x5d, 0xda, 0xdf, 0x74, 0x3f, 0x57, 0x27, 0xe6, 0x6b, 0xba, 0x8c, 0x2e, 0x38,
                0x79, 0x22, 0xd1, 0xaf, 0x43, 0xc5, 0x03, 0xd9, 0x11, 0x4a, 0x8f, 0xba, 0x10, 0x4d,
                0x84, 0xd0, 0x2b, 0xfa,
            ]),
            flags: AuthenticatorDataFlags::USER_PRESENT,
            counter: 0x11,
            credential_data: None,
            extensions: Default::default(),
        };

        let expected_assertion = Assertion {
            credentials: Some(PublicKeyCredentialDescriptor {
                id: vec![
                    242, 32, 6, 222, 79, 144, 90, 246, 138, 67, 148, 47, 2, 79, 42, 94, 206, 96,
                    61, 156, 109, 75, 61, 248, 190, 8, 237, 1, 252, 68, 38, 70, 208, 52, 133, 138,
                    199, 91, 237, 63, 213, 128, 191, 152, 8, 217, 79, 203, 238, 130, 185, 178, 239,
                    102, 119, 175, 10, 220, 195, 88, 82, 234, 107, 158,
                ],
                transports: vec![],
            }),
            signature: vec![
                0x30, 0x45, 0x02, 0x20, 0x4a, 0x5a, 0x9d, 0xd3, 0x92, 0x98, 0x14, 0x9d, 0x90, 0x47,
                0x69, 0xb5, 0x1a, 0x45, 0x14, 0x33, 0x00, 0x6f, 0x18, 0x2a, 0x34, 0xfb, 0xdf, 0x66,
                0xde, 0x5f, 0xc7, 0x17, 0xd7, 0x5f, 0xb3, 0x50, 0x02, 0x21, 0x00, 0xa4, 0x6b, 0x8e,
                0xa3, 0xc3, 0xb9, 0x33, 0x82, 0x1c, 0x6e, 0x7f, 0x5e, 0xf9, 0xda, 0xae, 0x94, 0xab,
                0x47, 0xf1, 0x8d, 0xb4, 0x74, 0xc7, 0x47, 0x90, 0xea, 0xab, 0xb1, 0x44, 0x11, 0xe7,
                0xa0,
            ],
            user: Some(PublicKeyCredentialUserEntity {
                id: vec![
                    0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02,
                    0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xa0, 0x03, 0x02, 0x01, 0x02,
                    0x30, 0x82, 0x01, 0x93, 0x30, 0x82,
                ],
                name: Some("johnpsmith@example.com".to_string()),
                display_name: Some("John P. Smith".to_string()),
            }),
            auth_data: expected_auth_data,
        };

        let expected = vec![GetAssertionResult {
            assertion: expected_assertion,
            attachment: AuthenticatorAttachment::Unknown,
            extensions: Default::default(),
            user_selected: None,
            large_blob_key: None,
            large_blob_array: None,
        }];
        let response = device.send_cbor(&assertion, None).unwrap();
        assert_eq!(response, expected);
    }

    #[test]
    fn test_serialize_get_assertion_ctap2() {
        let client_data = CollectedClientData {
            webauthn_type: WebauthnType::Create,
            challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
            origin: String::from("example.com"),
            cross_origin: false,
            token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
        };
        let assertion = GetAssertion {
            client_data_hash: client_data.hash().expect("failed to serialize client data"),
            rp: RelyingParty::from("example.com"),
            allow_list: vec![PublicKeyCredentialDescriptor {
                id: vec![
                    0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35,
                    0xEF, 0xAA, 0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D,
                    0xA4, 0x85, 0x34, 0xC8, 0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5,
                    0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD, 0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5,
                    0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08, 0xFE, 0x42, 0x00, 0x38,
                ],
                transports: vec![Transport::USB],
            }],
            extensions: GetAssertionExtensions {
                app_id: Some("https://example.com".to_string()),
                hmac_secret: Some(HmacGetSecretOrPrf::HmacGetSecret(
                    HmacSecretExtension::new_test(
                        vec![32; 32],
                        None,
                        CalculatedHmacSecretExtension {
                            public_key: COSEKey {
                                alg: COSEAlgorithm::ECDH_ES_HKDF256,
                                key: COSEKeyType::EC2(COSEEC2Key {
                                    curve: Curve::SECP256R1,
                                    x: vec![],
                                    y: vec![],
                                }),
                            },
                            salt_enc: vec![7; 32],
                            salt_auth: vec![8; 16],
                        },
                        None,
                    ),
                )),
                cred_blob: None,
                large_blob_key: None,
                third_party_payment: None,
            },
            options: GetAssertionOptions {
                user_presence: Some(true),
                user_verification: None,
            },
            pin_uv_auth_param: Some(PinUvAuthParam::create_empty()),
            enterprise_attestation: None,
            attestation_formats_preference: None,
        };
        let req_serialized = assertion
            .wire_format()
            .expect("Failed to serialize GetAssertion request");
        assert_eq!(
            req_serialized,
            [
                // Value copied from test failure output as regression test snapshot
                167, 1, 107, 101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, 2, 88, 32, 117, 53,
                53, 125, 73, 110, 51, 200, 24, 127, 234, 141, 17, 50, 100, 170, 164, 82, 62, 19,
                64, 20, 159, 190, 0, 63, 16, 135, 84, 195, 45, 128, 3, 129, 162, 98, 105, 100, 88,
                64, 62, 189, 137, 191, 119, 236, 80, 151, 85, 238, 156, 38, 53, 239, 170, 172, 123,
                43, 156, 92, 239, 23, 54, 195, 113, 125, 164, 133, 52, 200, 198, 182, 84, 215, 255,
                148, 95, 80, 181, 204, 78, 120, 5, 91, 221, 57, 107, 100, 247, 141, 162, 197, 249,
                98, 0, 204, 212, 21, 205, 8, 254, 66, 0, 56, 100, 116, 121, 112, 101, 106, 112,
                117, 98, 108, 105, 99, 45, 107, 101, 121, 4, 161, 107, 104, 109, 97, 99, 45, 115,
                101, 99, 114, 101, 116, 163, 1, 165, 1, 2, 3, 56, 24, 32, 1, 33, 64, 34, 64, 2, 88,
                32, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 3, 80, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 5, 161,
                98, 117, 112, 245, 6, 64, 7, 1
            ]
        );
    }

    #[test]
    fn test_serialize_get_assertion_ctap2_pin_protocol_2() {
        let assertion = GetAssertion {
            client_data_hash: ClientDataHash([0; 32]),
            rp: RelyingParty::from("example.com"),
            allow_list: vec![],
            extensions: GetAssertionExtensions {
                app_id: None,
                hmac_secret: Some(HmacGetSecretOrPrf::HmacGetSecret(
                    HmacSecretExtension::new_test(
                        vec![32; 32],
                        None,
                        CalculatedHmacSecretExtension {
                            public_key: COSEKey {
                                alg: COSEAlgorithm::ECDH_ES_HKDF256,
                                key: COSEKeyType::EC2(COSEEC2Key {
                                    curve: Curve::SECP256R1,
                                    x: vec![],
                                    y: vec![],
                                }),
                            },
                            salt_enc: vec![7; 32],
                            salt_auth: vec![8; 16],
                        },
                        Some(2),
                    ),
                )),
                cred_blob: None,
                large_blob_key: None,
                third_party_payment: None,
            },
            options: GetAssertionOptions {
                user_presence: None,
                user_verification: None,
            },
            pin_uv_auth_param: Some(PinUvAuthParam::create_test(
                2,
                vec![9; 4],
                PinUvAuthTokenPermission::GetAssertion,
            )),
            enterprise_attestation: None,
            attestation_formats_preference: None,
        };
        let req_serialized = assertion
            .wire_format()
            .expect("Failed to serialize GetAssertion request");
        assert_eq!(
            req_serialized,
            [
                // Value copied from test failure output as regression test snapshot
                165, 1, 107, 101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, 2, 88, 32, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 4, 161, 107, 104, 109, 97, 99, 45, 115, 101, 99, 114, 101, 116, 164, 1, 165, 1,
                2, 3, 56, 24, 32, 1, 33, 64, 34, 64, 2, 88, 32, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 80, 8, 8, 8, 8, 8,
                8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 2, 6, 68, 9, 9, 9, 9, 7, 2
            ]
        );
    }

    #[test]
    #[should_panic(
        expected = "PrfUninitialized must be replaced with Prf or PrfUnmatched before serializing"
    )]
    fn test_serialize_prf_uninitialized() {
        let assertion = GetAssertion {
            client_data_hash: ClientDataHash([0; 32]),
            rp: RelyingParty::from("example.com"),
            allow_list: vec![],
            extensions: GetAssertionExtensions {
                app_id: None,
                hmac_secret: Some(HmacGetSecretOrPrf::PrfUninitialized(
                    AuthenticationExtensionsPRFInputs {
                        eval: None,
                        eval_by_credential: None,
                    },
                )),
                cred_blob: None,
                large_blob_key: None,
                third_party_payment: None,
            },
            options: GetAssertionOptions {
                user_presence: None,
                user_verification: None,
            },
            pin_uv_auth_param: None,
            enterprise_attestation: None,
            attestation_formats_preference: None,
        };
        assertion
            .wire_format()
            .expect("Failed to serialize GetAssertion request");
    }

    #[test]
    fn test_serialize_prf_unmatched() {
        let assertion = GetAssertion {
            client_data_hash: ClientDataHash([0; 32]),
            rp: RelyingParty::from("example.com"),
            allow_list: vec![],
            extensions: GetAssertionExtensions {
                app_id: None,
                hmac_secret: Some(HmacGetSecretOrPrf::PrfUnmatched),
                cred_blob: None,
                large_blob_key: None,
                third_party_payment: None,
            },
            options: GetAssertionOptions {
                user_presence: None,
                user_verification: None,
            },
            pin_uv_auth_param: None,
            enterprise_attestation: None,
            attestation_formats_preference: None,
        };
        let req_serialized = assertion
            .wire_format()
            .expect("Failed to serialize GetAssertion request");
        assert_eq!(
            req_serialized,
            [
                // Value copied from test failure output as regression test snapshot
                163, 1, 107, 101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, 2, 88, 32, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 4, 160
            ]
        );
    }

    fn fill_device_ctap1_init(device: &mut Device, cid: [u8; 4]) {
        // init
        let mut msg = vec![0xFF, 0xFF, 0xFF, 0xFF]; // broadcast
        msg.extend([HIDCmd::Init.into(), 0x00, 8]); // cmd + bcnt
        msg.extend([0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01]); // nonce
        device.add_write(&msg, 0);

        let mut msg = vec![0xFF, 0xFF, 0xFF, 0xFF]; // broadcast
        msg.extend([0x06, 0x00, 17]); // cmd + bcnt
        msg.extend([0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01]); // nonce
        msg.extend(&cid);
        msg.push(2); // CTAPHID protocol version identifir
        msg.extend([1, 0, 0]); // Device version numbber
        msg.push(0x01); // CAPABILITY_WINK
        device.add_read(&msg, 0);
    }

    fn fill_device_ctap1(device: &mut Device, cid: [u8; 4], flags: u8, answer_status: [u8; 2]) {
        // ctap1 request
        let mut msg = cid.to_vec();
        msg.extend([HIDCmd::Msg.into(), 0x00, 0x8A]); // cmd + bcnt
        msg.extend([0x00, 0x2]); // U2F_AUTHENTICATE
        msg.extend([flags]);
        msg.extend([0x00, 0x00, 0x00]);
        msg.extend([0x81]); // Data len - 7
        msg.extend(CLIENT_DATA_HASH);
        msg.extend(&RELYING_PARTY_HASH[..18]);
        device.add_write(&msg, 0);

        // Continuation package
        let mut msg = cid.to_vec();
        msg.extend(vec![0x00]); // SEQ
        msg.extend(&RELYING_PARTY_HASH[18..]);
        msg.extend([KEY_HANDLE.len() as u8]);
        msg.extend(&KEY_HANDLE[..44]);
        device.add_write(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend(vec![0x01]); // SEQ
        msg.extend(&KEY_HANDLE[44..]);
        device.add_write(&msg, 0);

        // fido response
        let mut msg = cid.to_vec();
        msg.extend([HIDCmd::Msg.into(), 0x0, 0x4D]); // cmd + bcnt
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP1[0..57]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x0]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP1[57..]);
        msg.extend(answer_status);
        device.add_read(&msg, 0);
    }

    #[test]
    fn test_get_assertion_ctap1() {
        let client_data = CollectedClientData {
            webauthn_type: WebauthnType::Create,
            challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
            origin: String::from("example.com"),
            cross_origin: false,
            token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
        };
        let allowed_key = PublicKeyCredentialDescriptor {
            id: vec![
                0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35, 0xEF,
                0xAA, 0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D, 0xA4, 0x85,
                0x34, 0xC8, 0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78,
                0x05, 0x5B, 0xDD, 0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC,
                0xD4, 0x15, 0xCD, 0x08, 0xFE, 0x42, 0x00, 0x38,
            ],
            transports: vec![Transport::USB],
        };
        let mut assertion = GetAssertion::new(
            client_data.hash().expect("failed to serialize client data"),
            RelyingParty::from("example.com"),
            vec![allowed_key.clone()],
            GetAssertionOptions {
                user_presence: Some(true),
                user_verification: None,
            },
            Default::default(),
        );
        assert_canonical_cbor_encoding(&assertion);
        let mut device = Device::new("commands/get_assertion").unwrap(); // not really used (all functions ignore it)

        let mut cid = [0u8; 4];
        thread_rng().fill_bytes(&mut cid);

        fill_device_ctap1_init(&mut device, cid);
        HIDDevice::pre_init(&mut device).expect("pre_init");
        assert!(device.supports_ctap1());
        assert!(!device.supports_ctap2());
        device.downgrade_to_ctap1().expect("failed to downgrade");
        assert_eq!(device.get_protocol(), FidoProtocol::CTAP1);

        // ctap1 request
        let (tx, _rx) = channel();
        fill_device_ctap1(
            &mut device,
            cid,
            U2F_CHECK_IS_REGISTERED,
            SW_CONDITIONS_NOT_SATISFIED,
        );
        let key_handle = do_credential_list_filtering_ctap1(
            &mut device,
            &assertion.allow_list,
            &assertion.rp,
            &assertion.client_data_hash,
            &tx,
        )
        .expect("Did not find a key_handle, even though it should have");
        assertion.allow_list = vec![key_handle];
        let (ctap1_request, key_handle) = assertion.ctap1_format().unwrap();
        assert_eq!(key_handle, allowed_key);
        // Check if the request is going to be correct
        assert_eq!(ctap1_request, GET_ASSERTION_SAMPLE_REQUEST_CTAP1);

        // Now do it again, but parse the actual response
        // Pre-flighting is not done automatically
        fill_device_ctap1(&mut device, cid, U2F_REQUEST_USER_PRESENCE, SW_NO_ERROR);

        let response = device.send_ctap1(&assertion, None).unwrap();

        // Check if response is correct
        let expected_auth_data = AuthenticatorData {
            rp_id_hash: RpIdHash(RELYING_PARTY_HASH),
            flags: AuthenticatorDataFlags::USER_PRESENT,
            counter: 0x3B,
            credential_data: None,
            extensions: Default::default(),
        };

        let expected_assertion = Assertion {
            credentials: Some(allowed_key),
            signature: vec![
                0x30, 0x44, 0x02, 0x20, 0x7B, 0xDE, 0x0A, 0x52, 0xAC, 0x1F, 0x4C, 0x8B, 0x27, 0xE0,
                0x03, 0xA3, 0x70, 0xCD, 0x66, 0xA4, 0xC7, 0x11, 0x8D, 0xD2, 0x2D, 0x54, 0x47, 0x83,
                0x5F, 0x45, 0xB9, 0x9C, 0x68, 0x42, 0x3F, 0xF7, 0x02, 0x20, 0x3C, 0x51, 0x7B, 0x47,
                0x87, 0x7F, 0x85, 0x78, 0x2D, 0xE1, 0x00, 0x86, 0xA7, 0x83, 0xD1, 0xE7, 0xDF, 0x4E,
                0x36, 0x39, 0xE7, 0x71, 0xF5, 0xF6, 0xAF, 0xA3, 0x5A, 0xAD, 0x53, 0x73, 0x85, 0x8E,
            ],
            user: None,
            auth_data: expected_auth_data,
        };

        let expected = vec![GetAssertionResult {
            assertion: expected_assertion,
            attachment: AuthenticatorAttachment::Unknown,
            extensions: Default::default(),
            user_selected: None,
            large_blob_key: None,
            large_blob_array: None,
        }];
        assert_eq!(response, expected);
    }

    #[test]
    fn test_get_assertion_ctap1_long_keys() {
        let client_data = CollectedClientData {
            webauthn_type: WebauthnType::Create,
            challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
            origin: String::from("example.com"),
            cross_origin: false,
            token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
        };

        let too_long_key_handle = PublicKeyCredentialDescriptor {
            id: vec![0; 1000],
            transports: vec![Transport::USB],
        };
        let mut assertion = GetAssertion::new(
            client_data.hash().expect("failed to serialize client data"),
            RelyingParty::from("example.com"),
            vec![too_long_key_handle.clone()],
            GetAssertionOptions {
                user_presence: Some(true),
                user_verification: None,
            },
            Default::default(),
        );
        assert_canonical_cbor_encoding(&assertion);

        let mut device = Device::new("commands/get_assertion").unwrap(); // not really used (all functions ignore it)

        let mut cid = [0u8; 4];
        thread_rng().fill_bytes(&mut cid);

        fill_device_ctap1_init(&mut device, cid);
        HIDDevice::pre_init(&mut device).expect("pre_init");
        assert!(device.supports_ctap1());
        assert!(!device.supports_ctap2());
        device.downgrade_to_ctap1().expect("failed to downgrade");
        assert_eq!(device.get_protocol(), FidoProtocol::CTAP1);

        let (tx, _rx) = channel();
        assert!(matches!(
            do_credential_list_filtering_ctap1(
                &mut device,
                &assertion.allow_list,
                &assertion.rp,
                &assertion.client_data_hash,
                &tx,
            ),
            None
        ));
        assertion.allow_list = vec![];
        // It should also fail when trying to format
        assert!(matches!(
            assertion.ctap1_format(),
            Err(HIDError::Command(CommandError::StatusCode(
                StatusCode::NoCredentials,
                ..
            )))
        ));

        // Test also multiple too long keys and an empty allow list
        for allow_list in [vec![], vec![too_long_key_handle.clone(); 5]] {
            assertion.allow_list = allow_list;

            let (tx, _rx) = channel();
            assert!(matches!(
                do_credential_list_filtering_ctap1(
                    &mut device,
                    &assertion.allow_list,
                    &assertion.rp,
                    &assertion.client_data_hash,
                    &tx,
                ),
                None
            ));
        }

        let ok_key_handle = PublicKeyCredentialDescriptor {
            id: vec![
                0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35, 0xEF,
                0xAA, 0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D, 0xA4, 0x85,
                0x34, 0xC8, 0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78,
                0x05, 0x5B, 0xDD, 0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC,
                0xD4, 0x15, 0xCD, 0x08, 0xFE, 0x42, 0x00, 0x38,
            ],
            transports: vec![Transport::USB],
        };
        assertion.allow_list = vec![
            too_long_key_handle.clone(),
            too_long_key_handle.clone(),
            too_long_key_handle.clone(),
            ok_key_handle.clone(),
            too_long_key_handle,
        ];

        // ctap1 request
        fill_device_ctap1(
            &mut device,
            cid,
            U2F_CHECK_IS_REGISTERED,
            SW_CONDITIONS_NOT_SATISFIED,
        );
        let (tx, _rx) = channel();
        let key_handle = do_credential_list_filtering_ctap1(
            &mut device,
            &assertion.allow_list,
            &assertion.rp,
            &assertion.client_data_hash,
            &tx,
        )
        .expect("Did not find a key_handle, even though it should have");
        assertion.allow_list = vec![key_handle];
        let (ctap1_request, key_handle) = assertion.ctap1_format().unwrap();
        assert_eq!(key_handle, ok_key_handle);
        // Check if the request is going to be correct
        assert_eq!(ctap1_request, GET_ASSERTION_SAMPLE_REQUEST_CTAP1);

        // Now do it again, but parse the actual response
        // Pre-flighting is not done automatically
        fill_device_ctap1(&mut device, cid, U2F_REQUEST_USER_PRESENCE, SW_NO_ERROR);

        let response = device.send_ctap1(&assertion, None).unwrap();

        // Check if response is correct
        let expected_auth_data = AuthenticatorData {
            rp_id_hash: RpIdHash(RELYING_PARTY_HASH),
            flags: AuthenticatorDataFlags::USER_PRESENT,
            counter: 0x3B,
            credential_data: None,
            extensions: Default::default(),
        };

        let expected_assertion = Assertion {
            credentials: Some(ok_key_handle),
            signature: vec![
                0x30, 0x44, 0x02, 0x20, 0x7B, 0xDE, 0x0A, 0x52, 0xAC, 0x1F, 0x4C, 0x8B, 0x27, 0xE0,
                0x03, 0xA3, 0x70, 0xCD, 0x66, 0xA4, 0xC7, 0x11, 0x8D, 0xD2, 0x2D, 0x54, 0x47, 0x83,
                0x5F, 0x45, 0xB9, 0x9C, 0x68, 0x42, 0x3F, 0xF7, 0x02, 0x20, 0x3C, 0x51, 0x7B, 0x47,
                0x87, 0x7F, 0x85, 0x78, 0x2D, 0xE1, 0x00, 0x86, 0xA7, 0x83, 0xD1, 0xE7, 0xDF, 0x4E,
                0x36, 0x39, 0xE7, 0x71, 0xF5, 0xF6, 0xAF, 0xA3, 0x5A, 0xAD, 0x53, 0x73, 0x85, 0x8E,
            ],
            user: None,
            auth_data: expected_auth_data,
        };

        let expected = vec![GetAssertionResult {
            assertion: expected_assertion,
            attachment: AuthenticatorAttachment::Unknown,
            extensions: Default::default(),
            user_selected: None,
            large_blob_key: None,
            large_blob_array: None,
        }];
        assert_eq!(response, expected);
    }

    #[test]
    fn test_get_assertion_ctap2_pre_flight() {
        let client_data = CollectedClientData {
            webauthn_type: WebauthnType::Create,
            challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
            origin: String::from("example.com"),
            cross_origin: false,
            token_binding: Some(TokenBinding::Present(String::from("AAECAw"))),
        };
        let assertion = GetAssertion::new(
            client_data.hash().expect("failed to serialize client data"),
            RelyingParty::from("example.com"),
            vec![
                // This should never be tested, because it gets pre-filtered, since it is too long
                // (see max_credential_id_length)
                PublicKeyCredentialDescriptor {
                    id: vec![0x10; 100],
                    transports: vec![Transport::USB],
                },
                // One we test and skip
                PublicKeyCredentialDescriptor {
                    id: vec![
                        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                        0x11, 0x11, 0x11, 0x11,
                    ],
                    transports: vec![Transport::USB],
                },
                // This one is the 'right' one
                PublicKeyCredentialDescriptor {
                    id: vec![
                        0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26,
                        0x35, 0xEF, 0xAA, 0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3,
                        0x71, 0x7D, 0xA4, 0x85, 0x34, 0xC8, 0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94,
                        0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD, 0x39, 0x6B, 0x64,
                        0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08,
                        0xFE, 0x42, 0x00, 0x38,
                    ],
                    transports: vec![Transport::USB],
                },
                // We should never test this one
                PublicKeyCredentialDescriptor {
                    id: vec![
                        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                        0x22, 0x22, 0x22, 0x22,
                    ],
                    transports: vec![Transport::USB],
                },
            ],
            GetAssertionOptions {
                user_presence: Some(true),
                user_verification: None,
            },
            Default::default(),
        );
        assert_canonical_cbor_encoding(&assertion);
        let mut device = Device::new("commands/get_assertion").unwrap();
        assert_eq!(device.get_protocol(), FidoProtocol::CTAP2);
        let mut cid = [0u8; 4];
        thread_rng().fill_bytes(&mut cid);
        device.set_cid(cid);
        device.set_device_info(U2FDeviceInfo {
            vendor_name: Vec::new(),
            device_name: Vec::new(),
            version_interface: 0x02,
            version_major: 0x04,
            version_minor: 0x01,
            version_build: 0x08,
            cap_flags: Capability::WINK | Capability::CBOR,
        });
        assert!(device.supports_ctap1());
        assert!(device.supports_ctap2());
        device.set_authenticator_info(AuthenticatorInfo {
            versions: vec![AuthenticatorVersion::U2F_V2, AuthenticatorVersion::FIDO_2_0],
            extensions: vec!["uvm".to_string(), "hmac-secret".to_string()],
            aaguid: AAGuid(AAGUID_RAW),
            options: AuthenticatorOptions {
                platform_device: false,
                resident_key: true,
                client_pin: Some(false),
                user_presence: true,
                user_verification: None,
                ..Default::default()
            },
            max_msg_size: Some(1200),
            pin_protocols: Some(vec![1]),
            max_credential_count_in_list: None,
            max_credential_id_length: Some(80),
            transports: None,
            algorithms: None,
            max_ser_large_blob_array: None,
            force_pin_change: None,
            min_pin_length: None,
            firmware_version: None,
            max_cred_blob_length: None,
            max_rpids_for_set_min_pin_length: None,
            preferred_platform_uv_attempts: None,
            uv_modality: None,
            certifications: None,
            remaining_discoverable_credentials: None,
            vendor_prototype_config_commands: None,
            attestation_formats: None,
            uv_count_since_last_pin_entry: None,
            long_touch_for_reset: None,
        });

        // Sending first GetAssertion with first allow_list-entry, that will return an error
        let mut msg = cid.to_vec();
        msg.extend(vec![HIDCmd::Cbor.into(), 0x00, 0x90]);
        msg.extend(vec![0x2]); // u2f command
        msg.extend(vec![
            0xa4, // map(4)
            0x1,  // rpid
            0x6b, // text(11)
            101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, // example.com
            0x2, // clientDataHash
            0x58, 0x20, //bytes(32)
            0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f,
            0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b,
            0x78, 0x52, 0xb8, 0x55, // empty hash
            0x3,  //allowList
            0x81, // array(1)
            0xa2, // map(2)
            0x62, // text(2)
            0x69, 0x64, // id
            0x58, // bytes(
        ]);
        device.add_write(&msg, 0);

        msg = cid.to_vec();
        msg.extend([0x0]); //SEQ
        msg.extend([0x40]); // 64)
        msg.extend(&assertion.allow_list[1].id[..58]);
        device.add_write(&msg, 0);

        msg = cid.to_vec();
        msg.extend([0x1]); //SEQ
        msg.extend(&assertion.allow_list[1].id[58..64]);
        msg.extend(vec![
            0x64, // text(4),
            0x74, 0x79, 0x70, 0x65, // type
            0x6a, // text(10)
            0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x2D, 0x6B, 0x65, 0x79, // public-key
            0x5,  // options
            0xa1, // map(1)
            0x62, // text(2)
            0x75, 0x70, // up
            0xf4, // false
        ]);
        device.add_write(&msg, 0);

        // fido response
        let len = 0x1;
        let mut msg = cid.to_vec();
        msg.extend(vec![HIDCmd::Cbor.into(), 0x00, len]); // cmd + bcnt
        msg.push(0x2e); // Status code: NoCredentials
        device.add_read(&msg, 0);

        // Sending second GetAssertion with first allow_list-entry, that will return a success
        let mut msg = cid.to_vec();
        msg.extend(vec![HIDCmd::Cbor.into(), 0x00, 0x90]);
        msg.extend(vec![0x2]); // u2f command
        msg.extend(vec![
            0xa4, // map(4)
            0x1,  // rpid
            0x6b, // text(11)
            101, 120, 97, 109, 112, 108, 101, 46, 99, 111, 109, // example.com
            0x2, // clientDataHash
            0x58, 0x20, //bytes(32)
            0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f,
            0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b,
            0x78, 0x52, 0xb8, 0x55, // empty hash
            0x3,  //allowList
            0x81, // array(1)
            0xa2, // map(2)
            0x62, // text(2)
            0x69, 0x64, // id
            0x58, // bytes(
        ]);
        device.add_write(&msg, 0);

        msg = cid.to_vec();
        msg.extend([0x0]); //SEQ
        msg.extend([0x40]); // 64)
        msg.extend(&assertion.allow_list[2].id[..58]);
        device.add_write(&msg, 0);

        msg = cid.to_vec();
        msg.extend([0x1]); //SEQ
        msg.extend(&assertion.allow_list[2].id[58..64]);
        msg.extend(vec![
            0x64, // text(4),
            0x74, 0x79, 0x70, 0x65, // type
            0x6a, // text(10)
            0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x2D, 0x6B, 0x65, 0x79, // public-key
            0x5,  // options
            0xa1, // map(1)
            0x62, // text(2)
            0x75, 0x70, // up
            0xf4, // false
        ]);
        device.add_write(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([HIDCmd::Cbor.into(), 0x1, 0x2a]); // cmd + bcnt
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[..57]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x0]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[57..116]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x1]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[116..175]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x2]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[175..234]);
        device.add_read(&msg, 0);

        let mut msg = cid.to_vec();
        msg.extend([0x3]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[234..293]);
        device.add_read(&msg, 0);
        let mut msg = cid.to_vec();
        msg.extend([0x4]); // SEQ
        msg.extend(&GET_ASSERTION_SAMPLE_RESPONSE_CTAP2[293..]);
        device.add_read(&msg, 0);

        let (tx, _rx) = channel();
        assert!(matches!(
            do_credential_list_filtering_ctap2(
                &mut device,
                &assertion.allow_list,
                &assertion.rp,
                None,
                &tx,
            ),
            Ok(..)
        ));
    }

    #[test]
    fn test_get_assertion_ctap1_flags() {
        // Ensure that only the two low bits of flags are preserved when repackaging a
        // CTAP1 response.
        let mut sample = GET_ASSERTION_SAMPLE_RESPONSE_CTAP1.to_vec();
        sample[0] = 0xff; // Set all 8 flag bits before repackaging
        let add_info = PublicKeyCredentialDescriptor {
            id: vec![],
            transports: vec![],
        };
        let rp_hash = RpIdHash([0u8; 32]);
        let resp = GetAssertionResult::from_ctap1(&sample, &rp_hash, &add_info)
            .expect("could not handle response");
        assert_eq!(
            resp.assertion.auth_data.flags,
            AuthenticatorDataFlags::USER_PRESENT | AuthenticatorDataFlags::RESERVED_1
        );
    }

    // Manually assembled according to https://www.w3.org/TR/webauthn-2/#clientdatajson-serialization
    const CLIENT_DATA_VEC: [u8; 140] = [
        0x7b, 0x22, 0x74, 0x79, 0x70, 0x65, 0x22, 0x3a, // {"type":
        0x22, 0x77, 0x65, 0x62, 0x61, 0x75, 0x74, 0x68, 0x6e, 0x2e, 0x63, 0x72, 0x65, 0x61, 0x74,
        0x65, 0x22, // "webauthn.create"
        0x2c, 0x22, 0x63, 0x68, 0x61, 0x6c, 0x6c, 0x65, 0x6e, 0x67, 0x65, 0x22,
        0x3a, // (,"challenge":
        0x22, 0x41, 0x41, 0x45, 0x43, 0x41, 0x77, 0x22, // challenge in base64
        0x2c, 0x22, 0x6f, 0x72, 0x69, 0x67, 0x69, 0x6e, 0x22, 0x3a, // ,"origin":
        0x22, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d,
        0x22, // "example.com"
        0x2c, 0x22, 0x63, 0x72, 0x6f, 0x73, 0x73, 0x4f, 0x72, 0x69, 0x67, 0x69, 0x6e, 0x22,
        0x3a, // ,"crossOrigin":
        0x66, 0x61, 0x6c, 0x73, 0x65, // false
        0x2c, 0x22, 0x74, 0x6f, 0x6b, 0x65, 0x6e, 0x42, 0x69, 0x6e, 0x64, 0x69, 0x6e, 0x67, 0x22,
        0x3a, // ,"tokenBinding":
        0x7b, 0x22, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73, 0x22, 0x3a, // {"status":
        0x22, 0x70, 0x72, 0x65, 0x73, 0x65, 0x6e, 0x74, 0x22, // "present"
        0x2c, 0x22, 0x69, 0x64, 0x22, 0x3a, // ,"id":
        0x22, 0x41, 0x41, 0x45, 0x43, 0x41, 0x77, 0x22, // "AAECAw"
        0x7d, // }
        0x7d, // }
    ];

    const CLIENT_DATA_HASH: [u8; 32] = [
        0x75, 0x35, 0x35, 0x7d, 0x49, 0x6e, 0x33, 0xc8, 0x18, 0x7f, 0xea, 0x8d, 0x11, // hash
        0x32, 0x64, 0xaa, 0xa4, 0x52, 0x3e, 0x13, 0x40, 0x14, 0x9f, 0xbe, 0x00, 0x3f, // hash
        0x10, 0x87, 0x54, 0xc3, 0x2d, 0x80, // hash
    ];

    const RELYING_PARTY_HASH: [u8; 32] = [
        0xA3, 0x79, 0xA6, 0xF6, 0xEE, 0xAF, 0xB9, 0xA5, 0x5E, 0x37, 0x8C, 0x11, 0x80, 0x34, 0xE2,
        0x75, 0x1E, 0x68, 0x2F, 0xAB, 0x9F, 0x2D, 0x30, 0xAB, 0x13, 0xD2, 0x12, 0x55, 0x86, 0xCE,
        0x19, 0x47,
    ];
    const KEY_HANDLE: [u8; 64] = [
        0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35, 0xEF, 0xAA,
        0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D, 0xA4, 0x85, 0x34, 0xC8,
        0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD,
        0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08,
        0xFE, 0x42, 0x00, 0x38,
    ];

    const GET_ASSERTION_SAMPLE_REQUEST_CTAP1: [u8; 138] = [
        // CBOR Header
        0x0, // CLA
        0x2, // INS U2F_Authenticate
        0x3, // P1 Flags (user presence)
        0x0, // P2
        0x0, 0x0, 0x81, // Lc
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
        // Key Handle Length (1 Byte):
        0x40, // ..
        // Key Handle (Key Handle Length Bytes):
        0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35, 0xEF, 0xAA,
        0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D, 0xA4, 0x85, 0x34, 0xC8,
        0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD,
        0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08,
        0xFE, 0x42, 0x00, 0x38, // ..
        // Le (Ne=65536):
        0x0, 0x0,
    ];

    const GET_ASSERTION_SAMPLE_REQUEST_CTAP2: [u8; 138] = [
        // CBOR Header
        0x0, // leading zero
        0x2, // CMD U2F_Authenticate
        0x3, // Flags (user presence)
        0x0, 0x0, // zero bits
        0x0, 0x81, // size
        // NOTE: This has been taken from CTAP2.0 spec, but the clientDataHash has been replaced
        //       to be able to operate with known values for CollectedClientData (spec doesn't say
        //       what values led to the provided example hash)
        // clientDataHash:
        0x75, 0x35, 0x35, 0x7d, 0x49, 0x6e, 0x33, 0xc8, 0x18, 0x7f, 0xea, 0x8d, 0x11, 0x32, 0x64,
        0xaa, 0xa4, 0x52, 0x3e, 0x13, 0x40, 0x14, 0x9f, 0xbe, 0x00, 0x3f, 0x10, 0x87, 0x54, 0xc3,
        0x2d, 0x80, // hash
        // rpIdHash:
        0xA3, 0x79, 0xA6, 0xF6, 0xEE, 0xAF, 0xB9, 0xA5, 0x5E, 0x37, 0x8C, 0x11, 0x80, 0x34, 0xE2,
        0x75, 0x1E, 0x68, 0x2F, 0xAB, 0x9F, 0x2D, 0x30, 0xAB, 0x13, 0xD2, 0x12, 0x55, 0x86, 0xCE,
        0x19, 0x47, // ..
        // Key Handle Length (1 Byte):
        0x40, // ..
        // Key Handle (Key Handle Length Bytes):
        0x3E, 0xBD, 0x89, 0xBF, 0x77, 0xEC, 0x50, 0x97, 0x55, 0xEE, 0x9C, 0x26, 0x35, 0xEF, 0xAA,
        0xAC, 0x7B, 0x2B, 0x9C, 0x5C, 0xEF, 0x17, 0x36, 0xC3, 0x71, 0x7D, 0xA4, 0x85, 0x34, 0xC8,
        0xC6, 0xB6, 0x54, 0xD7, 0xFF, 0x94, 0x5F, 0x50, 0xB5, 0xCC, 0x4E, 0x78, 0x05, 0x5B, 0xDD,
        0x39, 0x6B, 0x64, 0xF7, 0x8D, 0xA2, 0xC5, 0xF9, 0x62, 0x00, 0xCC, 0xD4, 0x15, 0xCD, 0x08,
        0xFE, 0x42, 0x00, 0x38, 0x0, 0x0, // 2 trailing zeros from protocol
    ];

    const GET_ASSERTION_SAMPLE_RESPONSE_CTAP1: [u8; 75] = [
        0x01, // User Presence (1 Byte)
        0x00, 0x00, 0x00, 0x3B, // Sign Count (4 Bytes)
        // Signature (variable Length)
        0x30, 0x44, 0x02, 0x20, 0x7B, 0xDE, 0x0A, 0x52, 0xAC, 0x1F, 0x4C, 0x8B, 0x27, 0xE0, 0x03,
        0xA3, 0x70, 0xCD, 0x66, 0xA4, 0xC7, 0x11, 0x8D, 0xD2, 0x2D, 0x54, 0x47, 0x83, 0x5F, 0x45,
        0xB9, 0x9C, 0x68, 0x42, 0x3F, 0xF7, 0x02, 0x20, 0x3C, 0x51, 0x7B, 0x47, 0x87, 0x7F, 0x85,
        0x78, 0x2D, 0xE1, 0x00, 0x86, 0xA7, 0x83, 0xD1, 0xE7, 0xDF, 0x4E, 0x36, 0x39, 0xE7, 0x71,
        0xF5, 0xF6, 0xAF, 0xA3, 0x5A, 0xAD, 0x53, 0x73, 0x85, 0x8E,
    ];

    const GET_ASSERTION_SAMPLE_RESPONSE_CTAP2: [u8; 298] = [
        0x00, // status == success
        0xA5, // map(5)
        0x01, // unsigned(1)
        0xA2, // map(2)
        0x62, // text(2)
        0x69, 0x64, // "id"
        0x58, 0x40, // bytes(0x64, ) credential_id
        0xF2, 0x20, 0x06, 0xDE, 0x4F, 0x90, 0x5A, 0xF6, 0x8A, 0x43, 0x94, 0x2F, 0x02, 0x4F, 0x2A,
        0x5E, 0xCE, 0x60, 0x3D, 0x9C, 0x6D, 0x4B, 0x3D, 0xF8, 0xBE, 0x08, 0xED, 0x01, 0xFC, 0x44,
        0x26, 0x46, 0xD0, 0x34, 0x85, 0x8A, 0xC7, 0x5B, 0xED, 0x3F, 0xD5, 0x80, 0xBF, 0x98, 0x08,
        0xD9, 0x4F, 0xCB, 0xEE, 0x82, 0xB9, 0xB2, 0xEF, 0x66, 0x77, 0xAF, 0x0A, 0xDC, 0xC3, 0x58,
        0x52, 0xEA, 0x6B, 0x9E, // end: credential_id
        0x64, // text(4)
        0x74, 0x79, 0x70, 0x65, // "type"
        0x6A, // text(0x10, )
        0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x2D, 0x6B, 0x65, 0x79, // "public-key"
        0x02, // unsigned(2)
        0x58, 0x25, // bytes(0x37, ) auth_data
        0x62, 0x5D, 0xDA, 0xDF, 0x74, 0x3F, 0x57, 0x27, 0xE6, 0x6B, 0xBA, 0x8C, 0x2E, 0x38, 0x79,
        0x22, 0xD1, 0xAF, 0x43, 0xC5, 0x03, 0xD9, 0x11, 0x4A, 0x8F, 0xBA, 0x10, 0x4D, 0x84, 0xD0,
        0x2B, 0xFA, 0x01, 0x00, 0x00, 0x00, 0x11, // end: auth_data
        0x03, // unsigned(3)
        0x58, 0x47, // bytes(0x71, ) signature
        0x30, 0x45, 0x02, 0x20, 0x4A, 0x5A, 0x9D, 0xD3, 0x92, 0x98, 0x14, 0x9D, 0x90, 0x47, 0x69,
        0xB5, 0x1A, 0x45, 0x14, 0x33, 0x00, 0x6F, 0x18, 0x2A, 0x34, 0xFB, 0xDF, 0x66, 0xDE, 0x5F,
        0xC7, 0x17, 0xD7, 0x5F, 0xB3, 0x50, 0x02, 0x21, 0x00, 0xA4, 0x6B, 0x8E, 0xA3, 0xC3, 0xB9,
        0x33, 0x82, 0x1C, 0x6E, 0x7F, 0x5E, 0xF9, 0xDA, 0xAE, 0x94, 0xAB, 0x47, 0xF1, 0x8D, 0xB4,
        0x74, 0xC7, 0x47, 0x90, 0xEA, 0xAB, 0xB1, 0x44, 0x11, 0xE7, 0xA0, // end: signature
        0x04, // unsigned(4)
        0xA3, // map(3)
        0x62, // text(2)
        0x69, 0x64, // "id"
        0x58, 0x20, // bytes(0x32, ) user_id
        0x30, 0x82, 0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x30, 0x82,
        0x01, 0x93, 0x30, 0x82, 0x01, 0x38, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x30, 0x82, 0x01, 0x93,
        0x30, 0x82, // end: user_id
        0x64, // text(4)
        0x6E, 0x61, 0x6D, 0x65, // "name"
        0x76, // text(0x22, )
        0x6A, 0x6F, 0x68, 0x6E, 0x70, 0x73, 0x6D, 0x69, 0x74, 0x68, 0x40, 0x65, 0x78, 0x61, 0x6D,
        0x70, 0x6C, 0x65, 0x2E, 0x63, 0x6F, 0x6D, // "johnpsmith@example.com"
        0x6B, // text(0x11, )
        0x64, 0x69, 0x73, 0x70, 0x6C, 0x61, 0x79, 0x4E, 0x61, 0x6D, 0x65, // "displayName"
        0x6D, // text(0x13, )
        0x4A, 0x6F, 0x68, 0x6E, 0x20, 0x50, 0x2E, 0x20, 0x53, 0x6D, 0x69, 0x74,
        0x68, // "John P. Smith"
        0x05, // unsigned(5)
        0x01, // unsigned(1)
    ];

    /// Test canonical encoding of GetAssertion requests.
    #[test]
    fn test_cbor_canonical() {
        const OPTION_BOOL_VALUES: [Option<bool>; 3] = [None, Some(false), Some(true)];
        let allow_lists = vec![
            // Resident
            vec![],
            // Non-resident
            vec![PublicKeyCredentialDescriptor {
                id: vec![0x67; 67],
                transports: vec![Transport::USB],
            }],
        ];

        let client_data_hash = CollectedClientData {
            webauthn_type: WebauthnType::Get,
            challenge: Challenge::from(vec![0x00, 0x01, 0x02, 0x03]),
            origin: String::from("example.com"),
            cross_origin: false,
            token_binding: None,
        }
        .hash()
        .expect("failed to serialize client data");

        for allow_list in allow_lists {
            for user_presence in OPTION_BOOL_VALUES {
                for user_verification in OPTION_BOOL_VALUES {
                    let assertion = GetAssertion::new(
                        client_data_hash.clone(),
                        RelyingParty::from("example.com"),
                        allow_list.clone(),
                        GetAssertionOptions {
                            user_presence,
                            user_verification,
                        },
                        Default::default(),
                    );
                    assert_canonical_cbor_encoding(&assertion);
                }
            }
        }
    }

    mod hmac_secret {
        use std::convert::TryFrom;

        use crate::{
            crypto::{
                COSEAlgorithm, COSEEC2Key, COSEKey, COSEKeyType, Curve, PinUvAuthProtocol,
                SharedSecret,
            },
            ctap2::{
                attestation::{
                    AuthenticatorData, AuthenticatorDataFlags, Extension, HmacSecretResponse,
                },
                client_data::ClientDataHash,
                commands::{
                    get_assertion::{
                        CalculatedHmacSecretExtension, GetAssertion, GetAssertionExtensions,
                        HmacGetSecretOrPrf, HmacSecretExtension,
                    },
                    CommandError,
                },
                server::{
                    AuthenticationExtensionsClientOutputs, AuthenticationExtensionsPRFOutputs,
                    AuthenticatorAttachment, RelyingParty, RpIdHash,
                },
            },
            transport::platform::device::Device,
            Assertion, AuthenticatorInfo, FidoDevice, GetAssertionResult,
        };

        fn make_test_secret_without_puat(
            pin_protocol: u64,
        ) -> Result<(SharedSecret, COSEKey), CommandError> {
            let fake_client_key = COSEKey {
                alg: COSEAlgorithm::ECDH_ES_HKDF256,
                key: COSEKeyType::EC2(COSEEC2Key {
                    curve: Curve::SECP256R1,
                    x: vec![1],
                    y: vec![2],
                }),
            };
            let fake_peer_key = COSEKey {
                alg: COSEAlgorithm::ECDH_ES_HKDF256,
                key: COSEKeyType::EC2(COSEEC2Key {
                    curve: Curve::SECP256R1,
                    x: vec![3],
                    y: vec![4],
                }),
            };

            let pin_protocol = PinUvAuthProtocol::try_from(&AuthenticatorInfo {
                pin_protocols: Some(vec![pin_protocol]),
                ..Default::default()
            })?;

            let key = {
                let aes_key = 0..32;
                let hmac_key = 32..64;
                match pin_protocol.id() {
                    1 => aes_key.collect(),
                    2 => hmac_key.chain(aes_key).collect(),
                    _ => unimplemented!(),
                }
            };

            let shared_secret =
                SharedSecret::new_test(pin_protocol, key, fake_client_key.clone(), fake_peer_key);

            Ok((shared_secret, fake_client_key))
        }

        mod requires_crypto {
            use sha2::{Digest, Sha256};

            use super::*;
            use crate::{
                crypto::{CryptoError, PinUvAuthToken},
                ctap2::{
                    client_data::ClientDataHash,
                    commands::{
                        client_pin::PinUvAuthTokenPermission,
                        get_assertion::{
                            CalculatedHmacSecretExtension, GetAssertion, GetAssertionExtensions,
                            HmacGetSecretOrPrf, HmacSecretExtension,
                        },
                        PinUvAuthResult,
                    },
                    server::{
                        AuthenticationExtensionsPRFInputs, AuthenticationExtensionsPRFValues,
                        PublicKeyCredentialDescriptor, RelyingParty,
                    },
                },
                errors::AuthenticatorError,
            };

            fn make_test_secret(
                pin_protocol: u64,
            ) -> Result<(SharedSecret, COSEKey, PinUvAuthToken), CommandError> {
                let (shared_secret, fake_client_key) = make_test_secret_without_puat(pin_protocol)?;
                let puat = shared_secret.decrypt_pin_token(
                    PinUvAuthTokenPermission::empty(),
                    &shared_secret.encrypt(&[0x03; 32])?,
                )?;

                Ok((shared_secret, fake_client_key, puat))
            }

            fn get_assertion_process_hmac_secret(
                secret_available: bool,
                allow_list: Vec<PublicKeyCredentialDescriptor>,
                hmac_secret: Option<HmacGetSecretOrPrf>,
            ) -> Result<GetAssertion, AuthenticatorError> {
                let (shared_secret, _, puat) = make_test_secret(1)?;
                GetAssertion::new(
                    ClientDataHash([0x01; 32]),
                    RelyingParty::from("example.com"),
                    allow_list,
                    Default::default(),
                    GetAssertionExtensions {
                        hmac_secret,
                        ..Default::default()
                    },
                )
                .process_hmac_secret_and_prf_extension(
                    secret_available
                        .then_some((&shared_secret, &PinUvAuthResult::SuccessGetPinToken(puat))),
                )
            }

            #[test]
            fn get_assertion_hmac_secret_and_prf_absent_uses_no_input() {
                let get_assertion = get_assertion_process_hmac_secret(true, vec![], None).unwrap();
                assert!(matches!(get_assertion.extensions.hmac_secret, None));
            }

            #[test]
            fn get_assertion_prf_no_input_uses_unmatched_input() {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![],
                    Some(HmacGetSecretOrPrf::PrfUninitialized(
                        AuthenticationExtensionsPRFInputs {
                            eval: None,
                            eval_by_credential: None,
                        },
                    )),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::PrfUnmatched)
                ));
            }

            #[test]
            fn get_assertion_prf_no_secret_uses_unmatched_input() {
                let get_assertion = get_assertion_process_hmac_secret(
                    false,
                    vec![],
                    Some(HmacGetSecretOrPrf::PrfUninitialized(
                        AuthenticationExtensionsPRFInputs {
                            eval: Some(AuthenticationExtensionsPRFValues {
                                first: vec![1, 2, 3, 4],
                                second: None,
                            }),
                            eval_by_credential: None,
                        },
                    )),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::PrfUnmatched)
                ));
            }

            #[test]
            fn get_assertion_hmac_get_secret_uses_hmac_get_secret_input() {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![],
                    Some(HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                        vec![0x01; 32],
                        None,
                    ))),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension {
                        calculated_hmac: Some(_),
                        ..
                    }))
                ));
            }

            #[test]
            fn get_assertion_hmac_get_secret_bad_length_returns_invalid_input_error() {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![],
                    Some(HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                        vec![0x01; 31],
                        None,
                    ))),
                );
                assert!(matches!(
                    get_assertion,
                    Err(AuthenticatorError::InvalidRelyingPartyInput)
                ));
            }

            #[test]
            fn get_assertion_prf_eval_uses_eval_input() {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![],
                    Some(HmacGetSecretOrPrf::PrfUninitialized(
                        AuthenticationExtensionsPRFInputs {
                            eval: Some(AuthenticationExtensionsPRFValues {
                                first: vec![1, 2, 3, 4],
                                second: None,
                            }),
                            eval_by_credential: None,
                        },
                    )),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        salt1,
                        ..
                    })) if salt1 == Sha256::new_with_prefix(b"WebAuthn PRF")
                    .chain_update([0x00].iter())
                    .chain_update([1, 2, 3, 4].iter())
                    .finalize()
                    .to_vec()
                ));
            }

            #[test]
            fn get_assertion_prf_eval_by_credential_unmatched_uses_unmatched_input() {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![PublicKeyCredentialDescriptor {
                        id: vec![1, 2, 3, 4],
                        transports: vec![],
                    }],
                    Some(HmacGetSecretOrPrf::PrfUninitialized(
                        AuthenticationExtensionsPRFInputs {
                            eval: None,
                            eval_by_credential: Some(
                                [(
                                    vec![5, 6, 7, 8],
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![9, 10, 11, 12],
                                        second: None,
                                    },
                                )]
                                .into(),
                            ),
                        },
                    )),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::PrfUnmatched)
                ));
            }

            #[test]
            fn get_assertion_prf_eval_by_credential_matched_uses_eval_by_credential_input() {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![PublicKeyCredentialDescriptor {
                        id: vec![1, 2, 3, 4],
                        transports: vec![],
                    }],
                    Some(HmacGetSecretOrPrf::PrfUninitialized(
                        AuthenticationExtensionsPRFInputs {
                            eval: None,
                            eval_by_credential: Some(
                                [(
                                    vec![1, 2, 3, 4],
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![9, 10, 11, 12],
                                        second: None,
                                    },
                                )]
                                .into(),
                            ),
                        },
                    )),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        salt1,
                        ..
                    })) if salt1 == Sha256::new_with_prefix(b"WebAuthn PRF")
                    .chain_update([0x00].iter())
                    .chain_update([9, 10, 11, 12].iter())
                    .finalize()
                    .to_vec()
                ));
            }

            #[test]
            fn get_assertion_prf_eval_and_eval_by_credential_unmatched_uses_eval_input() {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![PublicKeyCredentialDescriptor {
                        id: vec![1, 2, 3, 4],
                        transports: vec![],
                    }],
                    Some(HmacGetSecretOrPrf::PrfUninitialized(
                        AuthenticationExtensionsPRFInputs {
                            eval: Some(AuthenticationExtensionsPRFValues {
                                first: vec![13, 14, 15, 16],
                                second: None,
                            }),
                            eval_by_credential: Some(
                                [(
                                    vec![5, 6, 7, 8],
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![9, 10, 11, 12],
                                        second: None,
                                    },
                                )]
                                .into(),
                            ),
                        },
                    )),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        salt1,
                        ..
                    })) if salt1 == Sha256::new_with_prefix(b"WebAuthn PRF")
                    .chain_update([0x00].iter())
                    .chain_update([13, 14, 15, 16].iter())
                    .finalize()
                    .to_vec()
                ));
            }

            #[test]
            fn get_assertion_prf_eval_and_eval_by_credential_matched_uses_eval_by_credential_input()
            {
                let get_assertion = get_assertion_process_hmac_secret(
                    true,
                    vec![PublicKeyCredentialDescriptor {
                        id: vec![1, 2, 3, 4],
                        transports: vec![],
                    }],
                    Some(HmacGetSecretOrPrf::PrfUninitialized(
                        AuthenticationExtensionsPRFInputs {
                            eval: Some(AuthenticationExtensionsPRFValues {
                                first: vec![13, 14, 15, 16],
                                second: None,
                            }),
                            eval_by_credential: Some(
                                [(
                                    vec![1, 2, 3, 4],
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![9, 10, 11, 12],
                                        second: None,
                                    },
                                )]
                                .into(),
                            ),
                        },
                    )),
                )
                .unwrap();
                assert!(matches!(
                    get_assertion.extensions.hmac_secret,
                    Some(HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        salt1,
                        ..
                    })) if salt1 == Sha256::new_with_prefix(b"WebAuthn PRF")
                    .chain_update([0x00].iter())
                    .chain_update([9, 10, 11, 12].iter())
                    .finalize()
                    .to_vec()
                ));
            }

            #[test]
            fn calculate_hmac_get_secret_pin_protocol_1() -> Result<(), AuthenticatorError> {
                let (shared_secret, client_key, puat) = make_test_secret(1)?;
                let extension = HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                    vec![0x01; 32],
                    Some(vec![0x02; 32]),
                ));
                let (extension, selected_cred) =
                    extension.calculate(&shared_secret, &[], Some(&puat))?;

                assert_eq!(selected_cred, None);
                assert_eq!(
                    extension,
                    HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension {
                        salt1: vec![0x01; 32],
                        salt2: Some(vec![0x02; 32]),
                        calculated_hmac: Some(CalculatedHmacSecretExtension {
                            public_key: client_key,
                            salt_enc: vec![
                                117, 226, 8, 41, 23, 33, 18, 187, 242, 160, 77, 61, 43, 18, 67, 61,
                                170, 97, 245, 245, 17, 42, 232, 186, 255, 190, 82, 1, 81, 152, 175,
                                39, 113, 130, 62, 169, 215, 202, 143, 80, 116, 195, 117, 22, 39,
                                64, 79, 110, 216, 117, 7, 144, 87, 73, 144, 75, 255, 173, 169, 201,
                                122, 160, 48, 157
                            ],
                            salt_auth: vec![
                                36, 74, 81, 146, 64, 28, 73, 44, 75, 111, 14, 79, 173, 146, 212,
                                227
                            ],
                        }),
                        pin_protocol: None,
                    }),
                );

                Ok(())
            }

            #[test]
            fn calculate_hmac_get_secret_wrong_length_salt1() -> Result<(), AuthenticatorError> {
                let (shared_secret, _, puat) = make_test_secret(1)?;
                for len in [0, 1, 31, 33, 64] {
                    let extension = HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                        vec![0x01; len],
                        None,
                    ));
                    let result = extension.calculate(&shared_secret, &[], Some(&puat));
                    assert_eq!(
                        result,
                        Err(CryptoError::WrongSaltLength),
                        "At salt1 length: {}",
                        len,
                    );
                }
                Ok(())
            }

            #[test]
            fn calculate_hmac_get_secret_wrong_length_salt2() -> Result<(), AuthenticatorError> {
                let (shared_secret, _, puat) = make_test_secret(1)?;
                for len in [0, 1, 31, 33, 64] {
                    let extension = HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                        vec![0x01; 32],
                        Some(vec![0x02; len]),
                    ));
                    let result = extension.calculate(&shared_secret, &[], Some(&puat));
                    assert_eq!(
                        result,
                        Err(CryptoError::WrongSaltLength),
                        "At salt2 length: {}",
                        len,
                    );
                }
                Ok(())
            }

            #[test]
            fn calculate_prf_eval_pin_protocol_1() -> Result<(), AuthenticatorError> {
                let (shared_secret, client_key, puat) = make_test_secret(1)?;
                let extension =
                    HmacGetSecretOrPrf::PrfUninitialized(AuthenticationExtensionsPRFInputs {
                        eval: Some(AuthenticationExtensionsPRFValues {
                            first: vec![0x01; 8],
                            second: Some(vec![0x02; 8]),
                        }),
                        eval_by_credential: None,
                    });
                let (extension, selected_cred) =
                    extension.calculate(&shared_secret, &[], Some(&puat))?;

                assert_eq!(selected_cred, None);
                assert_eq!(
                    extension,
                    HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        // JS: salt1 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(1)])))
                        salt1: vec![
                            5, 240, 179, 178, 62, 126, 205, 172, 176, 105, 211, 13, 86, 210, 48,
                            210, 225, 219, 234, 248, 16, 182, 52, 219, 92, 135, 97, 119, 107, 245,
                            30, 226
                        ],
                        // JS: salt2 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(2)])))
                        salt2: Some(vec![
                            96, 107, 65, 234, 77, 176, 251, 24, 193, 188, 98, 23, 59, 240, 212, 6,
                            104, 176, 40, 242, 104, 190, 32, 124, 226, 244, 19, 160, 8, 105, 253,
                            106
                        ]),
                        calculated_hmac: Some(CalculatedHmacSecretExtension {
                            public_key: client_key,
                            // JS: aesKey = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "AES-CBC" }, false, ["encrypt"])
                            // JS: salt_enc = new Uint8Array(await crypto.subtle.encrypt({ name: "AES-CBC", iv: new Uint8Array(16) }, aesKey, new Uint8Array([...salt1, ...salt2]))).slice(0, 64)
                            // (Need to strip trailing padding block inserted by WebCrypto)
                            salt_enc: vec![
                                23, 99, 220, 93, 59, 246, 109, 157, 247, 33, 138, 91, 142, 40, 203,
                                234, 96, 212, 26, 15, 56, 160, 191, 142, 138, 106, 2, 207, 219,
                                180, 39, 31, 155, 232, 119, 179, 0, 65, 9, 37, 184, 194, 135, 173,
                                187, 197, 51, 38, 68, 57, 197, 68, 249, 41, 143, 197, 46, 53, 72,
                                60, 109, 33, 112, 175
                            ],
                            // JS: hmacKeyP1 = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "HMAC", hash: "SHA-256" }, false, ["sign"])
                            // JS: salt_auth = new Uint8Array(await crypto.subtle.sign("HMAC", hmacKeyP1, salt_enc)).slice(0, 16)
                            salt_auth: vec![
                                27, 222, 224, 22, 170, 39, 171, 5, 98, 207, 176, 58, 23, 108, 223,
                                174
                            ],
                        }),
                        pin_protocol: None,
                    }),
                );

                Ok(())
            }

            #[test]
            fn calculate_prf_eval_by_cred_fallback_to_eval_pin_protocol_1(
            ) -> Result<(), AuthenticatorError> {
                let (shared_secret, client_key, puat) = make_test_secret(1)?;
                let extension =
                    HmacGetSecretOrPrf::PrfUninitialized(AuthenticationExtensionsPRFInputs {
                        eval: Some(AuthenticationExtensionsPRFValues {
                            first: vec![0x01; 8],
                            second: Some(vec![0x02; 8]),
                        }),
                        eval_by_credential: Some(
                            [(
                                vec![1, 2, 3, 4],
                                AuthenticationExtensionsPRFValues {
                                    first: vec![0x04; 8],
                                    second: Some(vec![0x05; 8]),
                                },
                            )]
                            .iter()
                            .cloned()
                            .collect(),
                        ),
                    });
                let allow_list = [PublicKeyCredentialDescriptor {
                    id: vec![5, 6, 7, 8],
                    transports: vec![],
                }];
                let (extension, selected_cred) =
                    extension.calculate(&shared_secret, &allow_list, Some(&puat))?;

                assert_eq!(selected_cred, None);
                assert_eq!(
                    extension,
                    HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        // JS: salt1 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(1)])))
                        salt1: vec![
                            5, 240, 179, 178, 62, 126, 205, 172, 176, 105, 211, 13, 86, 210, 48,
                            210, 225, 219, 234, 248, 16, 182, 52, 219, 92, 135, 97, 119, 107, 245,
                            30, 226
                        ],
                        // JS: salt2 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(2)])))
                        salt2: Some(vec![
                            96, 107, 65, 234, 77, 176, 251, 24, 193, 188, 98, 23, 59, 240, 212, 6,
                            104, 176, 40, 242, 104, 190, 32, 124, 226, 244, 19, 160, 8, 105, 253,
                            106
                        ]),
                        calculated_hmac: Some(CalculatedHmacSecretExtension {
                            public_key: client_key,
                            // JS: aesKey = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "AES-CBC" }, false, ["encrypt"])
                            // JS: salt_enc = new Uint8Array(await crypto.subtle.encrypt({ name: "AES-CBC", iv: new Uint8Array(16) }, aesKey, new Uint8Array([...salt1, ...salt2]))).slice(0, 64)
                            // (Need to strip trailing padding block inserted by WebCrypto)
                            salt_enc: vec![
                                23, 99, 220, 93, 59, 246, 109, 157, 247, 33, 138, 91, 142, 40, 203,
                                234, 96, 212, 26, 15, 56, 160, 191, 142, 138, 106, 2, 207, 219,
                                180, 39, 31, 155, 232, 119, 179, 0, 65, 9, 37, 184, 194, 135, 173,
                                187, 197, 51, 38, 68, 57, 197, 68, 249, 41, 143, 197, 46, 53, 72,
                                60, 109, 33, 112, 175
                            ],
                            // JS: hmacKeyP1 = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "HMAC", hash: "SHA-256" }, false, ["sign"])
                            // JS: salt_auth = new Uint8Array(await crypto.subtle.sign("HMAC", hmacKeyP1, salt_enc)).slice(0, 16)
                            salt_auth: vec![
                                27, 222, 224, 22, 170, 39, 171, 5, 98, 207, 176, 58, 23, 108, 223,
                                174
                            ],
                        }),
                        pin_protocol: None,
                    }),
                );

                Ok(())
            }

            #[test]
            fn calculate_prf_eval_by_cred_pin_protocol_1() -> Result<(), AuthenticatorError> {
                let (shared_secret, client_key, puat) = make_test_secret(1)?;
                let cred_id = PublicKeyCredentialDescriptor {
                    id: vec![1, 2, 3, 4],
                    transports: vec![],
                };
                let extension =
                    HmacGetSecretOrPrf::PrfUninitialized(AuthenticationExtensionsPRFInputs {
                        eval: Some(AuthenticationExtensionsPRFValues {
                            first: vec![0x01; 8],
                            second: Some(vec![0x02; 8]),
                        }),
                        eval_by_credential: Some(
                            [
                                (
                                    vec![9, 10, 11, 12],
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![0x06; 8],
                                        second: Some(vec![0x07; 8]),
                                    },
                                ),
                                (
                                    cred_id.id.clone(),
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![0x04; 8],
                                        second: Some(vec![0x05; 8]),
                                    },
                                ),
                            ]
                            .iter()
                            .cloned()
                            .collect(),
                        ),
                    });
                let allow_list = [
                    PublicKeyCredentialDescriptor {
                        id: vec![5, 6, 7, 8],
                        transports: vec![],
                    },
                    cred_id,
                    PublicKeyCredentialDescriptor {
                        id: vec![9, 10, 11, 12],
                        transports: vec![],
                    },
                ];
                let (extension, selected_cred) =
                    extension.calculate(&shared_secret, &allow_list, Some(&puat))?;

                assert_eq!(selected_cred, Some(&allow_list[1]));
                assert_eq!(
                    extension,
                    HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        // JS: salt1 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(4)])))
                        salt1: vec![
                            141, 49, 215, 240, 110, 193, 84, 27, 113, 153, 129, 108, 71, 59, 98, 5,
                            209, 45, 190, 142, 47, 4, 72, 78, 217, 85, 99, 243, 192, 217, 232, 88
                        ],
                        // JS: salt2 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(5)])))
                        salt2: Some(vec![
                            156, 88, 127, 151, 204, 90, 145, 200, 207, 201, 106, 124, 19, 60, 29,
                            115, 145, 197, 27, 148, 117, 72, 18, 4, 78, 187, 161, 122, 144, 245,
                            67, 1
                        ]),
                        calculated_hmac: Some(CalculatedHmacSecretExtension {
                            public_key: client_key,
                            // JS: aesKey = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "AES-CBC" }, false, ["encrypt"])
                            // JS: salt_enc = new Uint8Array(await crypto.subtle.encrypt({ name: "AES-CBC", iv: new Uint8Array(16) }, aesKey, new Uint8Array([...salt1, ...salt2]))).slice(0, 64)
                            // (Need to strip trailing padding block inserted by WebCrypto)
                            salt_enc: vec![
                                191, 228, 209, 183, 255, 132, 169, 88, 82, 9, 102, 239, 99, 201,
                                47, 15, 174, 24, 191, 30, 80, 230, 67, 237, 178, 112, 105, 243, 53,
                                209, 25, 189, 32, 51, 75, 255, 176, 160, 82, 113, 250, 141, 83,
                                130, 69, 156, 230, 91, 95, 17, 149, 11, 81, 40, 23, 42, 24, 33, 25,
                                167, 210, 241, 238, 237
                            ],
                            // JS: hmacKeyP1 = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "HMAC", hash: "SHA-256" }, false, ["sign"])
                            // JS: salt_auth = new Uint8Array(await crypto.subtle.sign("HMAC", hmacKeyP1, salt_enc)).slice(0, 16)
                            salt_auth: vec![
                                211, 87, 229, 38, 186, 254, 65, 2, 69, 166, 122, 30, 84, 77, 116,
                                232
                            ],
                        }),
                        pin_protocol: None,
                    }),
                );

                Ok(())
            }

            #[test]
            fn calculate_prf_only_eval_by_cred_pin_protocol_1() -> Result<(), AuthenticatorError> {
                let (shared_secret, client_key, puat) = make_test_secret(1)?;
                let cred_id = PublicKeyCredentialDescriptor {
                    id: vec![1, 2, 3, 4],
                    transports: vec![],
                };
                let extension =
                    HmacGetSecretOrPrf::PrfUninitialized(AuthenticationExtensionsPRFInputs {
                        eval: None,
                        eval_by_credential: Some(
                            [
                                (
                                    vec![9, 10, 11, 12],
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![0x06; 8],
                                        second: Some(vec![0x07; 8]),
                                    },
                                ),
                                (
                                    cred_id.id.clone(),
                                    AuthenticationExtensionsPRFValues {
                                        first: vec![0x04; 8],
                                        second: Some(vec![0x05; 8]),
                                    },
                                ),
                            ]
                            .iter()
                            .cloned()
                            .collect(),
                        ),
                    });
                let allow_list = [
                    PublicKeyCredentialDescriptor {
                        id: vec![5, 6, 7, 8],
                        transports: vec![],
                    },
                    cred_id,
                    PublicKeyCredentialDescriptor {
                        id: vec![9, 10, 11, 12],
                        transports: vec![],
                    },
                ];
                let (extension, selected_cred) =
                    extension.calculate(&shared_secret, &allow_list, Some(&puat))?;

                assert_eq!(selected_cred, Some(&allow_list[1]));
                assert_eq!(
                    extension,
                    HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                        // JS: salt1 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(4)])))
                        salt1: vec![
                            141, 49, 215, 240, 110, 193, 84, 27, 113, 153, 129, 108, 71, 59, 98, 5,
                            209, 45, 190, 142, 47, 4, 72, 78, 217, 85, 99, 243, 192, 217, 232, 88
                        ],
                        // JS: salt2 = new Uint8Array(await crypto.subtle.digest("SHA-256", new Uint8Array([...new TextEncoder().encode("WebAuthn PRF"), 0, ...new Uint8Array(8).fill(5)])))
                        salt2: Some(vec![
                            156, 88, 127, 151, 204, 90, 145, 200, 207, 201, 106, 124, 19, 60, 29,
                            115, 145, 197, 27, 148, 117, 72, 18, 4, 78, 187, 161, 122, 144, 245,
                            67, 1
                        ]),
                        calculated_hmac: Some(CalculatedHmacSecretExtension {
                            public_key: client_key,
                            // JS: aesKey = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "AES-CBC" }, false, ["encrypt"])
                            // JS: salt_enc = new Uint8Array(await crypto.subtle.encrypt({ name: "AES-CBC", iv: new Uint8Array(16) }, aesKey, new Uint8Array([...salt1, ...salt2]))).slice(0, 64)
                            // (Need to strip trailing padding block inserted by WebCrypto)
                            salt_enc: vec![
                                191, 228, 209, 183, 255, 132, 169, 88, 82, 9, 102, 239, 99, 201,
                                47, 15, 174, 24, 191, 30, 80, 230, 67, 237, 178, 112, 105, 243, 53,
                                209, 25, 189, 32, 51, 75, 255, 176, 160, 82, 113, 250, 141, 83,
                                130, 69, 156, 230, 91, 95, 17, 149, 11, 81, 40, 23, 42, 24, 33, 25,
                                167, 210, 241, 238, 237
                            ],
                            // JS: hmacKeyP1 = await crypto.subtle.importKey("raw", new Uint8Array(32).map((b, i) => i), { name: "HMAC", hash: "SHA-256" }, false, ["sign"])
                            // JS: salt_auth = new Uint8Array(await crypto.subtle.sign("HMAC", hmacKeyP1, salt_enc)).slice(0, 16)
                            salt_auth: vec![
                                211, 87, 229, 38, 186, 254, 65, 2, 69, 166, 122, 30, 84, 77, 116,
                                232
                            ],
                        }),
                        pin_protocol: None,
                    }),
                );

                Ok(())
            }

            #[test]
            fn calculate_prf_unmatched_pin_protocol_1() -> Result<(), AuthenticatorError> {
                let (shared_secret, _, puat) = make_test_secret(1)?;
                let extension =
                    HmacGetSecretOrPrf::PrfUninitialized(AuthenticationExtensionsPRFInputs {
                        eval: None,
                        eval_by_credential: Some(
                            [(
                                vec![1, 2, 3, 4],
                                AuthenticationExtensionsPRFValues {
                                    first: vec![0x04; 8],
                                    second: Some(vec![0x05; 8]),
                                },
                            )]
                            .iter()
                            .cloned()
                            .collect(),
                        ),
                    });
                let allow_list = [PublicKeyCredentialDescriptor {
                    id: vec![5, 6, 7, 8],
                    transports: vec![],
                }];
                let (extension, selected_cred) =
                    extension.calculate(&shared_secret, &allow_list, Some(&puat))?;

                assert_eq!(selected_cred, None);
                assert_eq!(extension, HmacGetSecretOrPrf::PrfUnmatched);

                Ok(())
            }

            #[test]
            fn calculate_prf_unmatched_pin_protocol_2() -> Result<(), AuthenticatorError> {
                let (shared_secret, _, puat) = make_test_secret(2)?;
                let extension =
                    HmacGetSecretOrPrf::PrfUninitialized(AuthenticationExtensionsPRFInputs {
                        eval: None,
                        eval_by_credential: Some(
                            [(
                                vec![1, 2, 3, 4],
                                AuthenticationExtensionsPRFValues {
                                    first: vec![0x04; 8],
                                    second: Some(vec![0x05; 8]),
                                },
                            )]
                            .iter()
                            .cloned()
                            .collect(),
                        ),
                    });
                let allow_list = [PublicKeyCredentialDescriptor {
                    id: vec![5, 6, 7, 8],
                    transports: vec![],
                }];
                let (extension, selected_cred) =
                    extension.calculate(&shared_secret, &allow_list, Some(&puat))?;

                assert_eq!(selected_cred, None);
                assert_eq!(extension, HmacGetSecretOrPrf::PrfUnmatched);

                Ok(())
            }

            #[test]
            fn finalize_result_hmac_get_secret_input_with_secret_output_becomes_client_output() {
                let result = finalize_result_with_hmac_secret_input_and_output(
                    Some(HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                        vec![],
                        None,
                    ))),
                    Some(HmacSecretResponse::Secret(vec![0x01; ONE_OUTPUT_LEN_PP2])),
                )
                .expect("Failed to run test");
                assert!(matches!(
                    result.extensions,
                    AuthenticationExtensionsClientOutputs {
                        hmac_get_secret: Some(_),
                        prf: None,
                        ..
                    }
                ));
            }

            #[test]
            fn finalize_result_prf_input_with_secret_output_becomes_results_output() {
                let result = finalize_result_with_hmac_secret_input_and_output(
                    Some(HmacGetSecretOrPrf::Prf(HmacSecretExtension::new(
                        vec![],
                        None,
                    ))),
                    Some(HmacSecretResponse::Secret(vec![0x01; ONE_OUTPUT_LEN_PP2])),
                )
                .expect("Failed to run test");
                assert!(matches!(
                    result.extensions,
                    AuthenticationExtensionsClientOutputs {
                        hmac_get_secret: None,
                        prf: Some(AuthenticationExtensionsPRFOutputs {
                            enabled: None,
                            results: Some(_),
                        }),
                        ..
                    }
                ));
            }
        }

        #[test]
        #[should_panic(
            expected = "unreachable code: hmac-secret inputs from PRF already initialized"
        )]
        fn calculate_prf_conflict_1() {
            let (shared_secret, _) = make_test_secret_without_puat(2).unwrap();
            let extension = HmacGetSecretOrPrf::PrfUnmatched;
            extension.calculate(&shared_secret, &[], None).unwrap();
        }

        #[test]
        #[should_panic(
            expected = "unreachable code: hmac-secret inputs from PRF already initialized"
        )]
        fn calculate_prf_conflict_2() {
            let (shared_secret, client_key) = make_test_secret_without_puat(2).unwrap();
            let extension = HmacGetSecretOrPrf::Prf(HmacSecretExtension {
                salt1: vec![],
                salt2: Some(vec![]),
                calculated_hmac: Some(CalculatedHmacSecretExtension {
                    public_key: client_key,
                    salt_enc: vec![],
                    salt_auth: vec![],
                }),
                pin_protocol: None,
            });
            extension.calculate(&shared_secret, &[], None).unwrap();
        }

        fn finalize_result_with_hmac_secret_input_and_output(
            hmac_secret_input: Option<HmacGetSecretOrPrf>,
            hmac_secret_response: Option<HmacSecretResponse>,
        ) -> Result<GetAssertionResult, CommandError> {
            let get_assertion = GetAssertion::new(
                ClientDataHash([0x01; 32]),
                RelyingParty::from("example.com"),
                vec![],
                Default::default(),
                GetAssertionExtensions {
                    hmac_secret: hmac_secret_input,
                    ..Default::default()
                },
            );
            let mut result = GetAssertionResult {
                assertion: Assertion {
                    credentials: None,
                    auth_data: AuthenticatorData {
                        rp_id_hash: RpIdHash([0x01; 32]),
                        flags: AuthenticatorDataFlags::empty(),
                        counter: 0,
                        credential_data: None,
                        extensions: Extension {
                            cred_protect: None,
                            hmac_secret: hmac_secret_response,
                            min_pin_length: None,
                            cred_blob: None,
                        },
                    },
                    signature: vec![],
                    user: None,
                },
                attachment: AuthenticatorAttachment::Unknown,
                extensions: AuthenticationExtensionsClientOutputs::default(),
                user_selected: None,
                large_blob_key: None,
                large_blob_array: None,
            };

            let mut dev = Device::new_skipping_serialization("commands/get_assertion")
                .expect("Failed to create mock Device");
            let (shared_secret, _) = make_test_secret_without_puat(2)?;
            dev.set_shared_secret(shared_secret);
            get_assertion.finalize_result(&dev, &mut result);
            Ok(result)
        }

        /// Encrypted salt output for pin protocol 2: iv || ct
        const ONE_OUTPUT_LEN_PP2: usize = 16 + 32;

        #[test]
        fn finalize_result_no_input_with_no_output_becomes_no_client_output() {
            let result = finalize_result_with_hmac_secret_input_and_output(None, None)
                .expect("Failed to run test");
            assert!(matches!(result.extensions.hmac_get_secret, None));
        }

        #[test]
        fn finalize_result_no_input_with_secret_output_becomes_no_client_output() {
            let result = finalize_result_with_hmac_secret_input_and_output(
                None,
                Some(HmacSecretResponse::Secret(vec![0x01; ONE_OUTPUT_LEN_PP2])),
            )
            .expect("Failed to run test");
            assert!(matches!(
                result.extensions,
                AuthenticationExtensionsClientOutputs {
                    hmac_get_secret: None,
                    prf: None,
                    ..
                }
            ));
        }

        #[test]
        fn finalize_result_hmac_get_secret_input_with_no_output_becomes_no_client_output() {
            let result = finalize_result_with_hmac_secret_input_and_output(
                Some(HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                    vec![],
                    None,
                ))),
                None,
            )
            .expect("Failed to run test");
            assert!(matches!(
                result.extensions,
                AuthenticationExtensionsClientOutputs {
                    hmac_get_secret: None,
                    prf: None,
                    ..
                }
            ));
        }

        #[test]
        fn finalize_result_hmac_get_secret_input_with_confirmed_output_becomes_no_client_output() {
            let result = finalize_result_with_hmac_secret_input_and_output(
                Some(HmacGetSecretOrPrf::HmacGetSecret(HmacSecretExtension::new(
                    vec![],
                    None,
                ))),
                Some(HmacSecretResponse::Confirmed(true)),
            )
            .expect("Failed to run test");
            assert!(matches!(
                result.extensions,
                AuthenticationExtensionsClientOutputs {
                    hmac_get_secret: None,
                    prf: None,
                    ..
                }
            ));
        }

        #[test]
        fn finalize_result_prf_input_with_no_output_becomes_empty_client_output() {
            let result = finalize_result_with_hmac_secret_input_and_output(
                Some(HmacGetSecretOrPrf::Prf(HmacSecretExtension::new(
                    vec![],
                    None,
                ))),
                None,
            )
            .expect("Failed to run test");
            assert!(matches!(
                result.extensions,
                AuthenticationExtensionsClientOutputs {
                    hmac_get_secret: None,
                    prf: Some(AuthenticationExtensionsPRFOutputs {
                        enabled: None,
                        results: None,
                    }),
                    ..
                }
            ));
        }

        #[test]
        fn finalize_result_prf_input_with_confirmed_output_becomes_empty_client_output() {
            let result = finalize_result_with_hmac_secret_input_and_output(
                Some(HmacGetSecretOrPrf::Prf(HmacSecretExtension::new(
                    vec![],
                    None,
                ))),
                Some(HmacSecretResponse::Confirmed(true)),
            )
            .expect("Failed to run test");
            assert!(matches!(
                result.extensions,
                AuthenticationExtensionsClientOutputs {
                    hmac_get_secret: None,
                    prf: Some(AuthenticationExtensionsPRFOutputs {
                        enabled: None,
                        results: None,
                    }),
                    ..
                }
            ));
        }
    }
}
