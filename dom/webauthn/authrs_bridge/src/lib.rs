/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#[macro_use]
extern crate log;

#[macro_use]
extern crate xpcom;

use authenticator::{
    authenticatorservice::{RegisterArgs, SignArgs},
    ctap2::attestation::{AAGuid, AttestationObject, AttestationStatement},
    ctap2::commands::{get_info::AuthenticatorVersion, PinUvAuthResult},
    ctap2::server::{
        AuthenticationExtensionsClientInputs, AuthenticationExtensionsPRFInputs,
        AuthenticationExtensionsPRFOutputs, AuthenticationExtensionsPRFValues,
        AuthenticatorAttachment, CredentialProtectionPolicy, PublicKeyCredentialDescriptor,
        PublicKeyCredentialParameters, PublicKeyCredentialUserEntity, RelyingParty,
        ResidentKeyRequirement, UserVerificationRequirement,
    },
    errors::AuthenticatorError,
    statecallback::StateCallback,
    AuthenticatorInfo, BioEnrollmentResult, CredentialManagementResult, InteractiveRequest,
    ManageResult, Pin, RegisterResult, SignResult, StateMachine, StatusPinUv, StatusUpdate,
};
use base64::Engine;
use cstr::cstr;
use moz_task::{get_main_thread, RunnableBuilder};
use nserror::{
    nsresult, NS_ERROR_DOM_ABORT_ERR, NS_ERROR_DOM_INVALID_STATE_ERR, NS_ERROR_DOM_NOT_ALLOWED_ERR,
    NS_ERROR_DOM_OPERATION_ERR, NS_ERROR_FAILURE, NS_ERROR_INVALID_ARG, NS_ERROR_NOT_AVAILABLE,
    NS_ERROR_NOT_IMPLEMENTED, NS_ERROR_NULL_POINTER, NS_OK,
};
use nsstring::{nsACString, nsAString, nsCString, nsString};
use serde::Serialize;
use serde_json::json;
use std::cell::RefCell;
use std::collections::HashMap;
use std::fmt::Write;
use std::sync::mpsc::{channel, Receiver, RecvError, Sender};
use std::sync::{Arc, Mutex, MutexGuard};
use thin_vec::{thin_vec, ThinVec};
use xpcom::interfaces::{
    nsICredentialParameters, nsIObserverService, nsIWebAuthnAttObj, nsIWebAuthnAutoFillEntry,
    nsIWebAuthnRegisterArgs, nsIWebAuthnRegisterPromise, nsIWebAuthnRegisterResult,
    nsIWebAuthnService, nsIWebAuthnSignArgs, nsIWebAuthnSignPromise, nsIWebAuthnSignResult,
};
use xpcom::{xpcom_method, RefPtr};
mod about_webauthn_controller;
use about_webauthn_controller::*;
mod test_token;
use test_token::TestTokenManager;

fn authrs_to_nserror(e: AuthenticatorError) -> nsresult {
    match e {
        AuthenticatorError::CredentialExcluded => NS_ERROR_DOM_INVALID_STATE_ERR,
        _ => NS_ERROR_DOM_NOT_ALLOWED_ERR,
    }
}

fn should_cancel_prompts<T>(result: &Result<T, AuthenticatorError>) -> bool {
    match result {
        Err(AuthenticatorError::CredentialExcluded) | Err(AuthenticatorError::PinError(_)) => false,
        _ => true,
    }
}

// Using serde(tag="type") makes it so that, for example, BrowserPromptType::Cancel is serialized
// as '{ type: "cancel" }', and BrowserPromptType::PinInvalid { retries: 5 } is serialized as
// '{type: "pin-invalid", retries: 5}'.
#[derive(Serialize)]
#[serde(tag = "type", rename_all = "kebab-case")]
enum BrowserPromptType<'a> {
    AlreadyRegistered,
    Cancel,
    DeviceBlocked,
    PinAuthBlocked,
    PinNotSet,
    Presence,
    SelectDevice,
    UvBlocked,
    PinRequired,
    SelectedDevice {
        auth_info: Option<AuthenticatorInfo>,
    },
    PinInvalid {
        retries: Option<u8>,
    },
    PinIsTooLong,
    PinIsTooShort,
    UvInvalid {
        retries: Option<u8>,
    },
    SelectSignResult {
        entities: &'a [PublicKeyCredentialUserEntity],
    },
    ListenSuccess,
    ListenError {
        error: Box<BrowserPromptType<'a>>,
    },
    CredentialManagementUpdate {
        result: CredentialManagementResult,
    },
    BioEnrollmentUpdate {
        result: BioEnrollmentResult,
    },
    UnknownError,
}

#[derive(Debug)]
enum PromptTarget {
    Browser,
    AboutPage,
}

#[derive(Serialize)]
struct BrowserPromptMessage<'a> {
    prompt: BrowserPromptType<'a>,
    tid: u64,
    origin: Option<&'a str>,
    #[serde(rename = "browsingContextId")]
    browsing_context_id: Option<u64>,
}

fn notify_observers(prompt_target: PromptTarget, json: nsString) -> Result<(), nsresult> {
    let main_thread = get_main_thread()?;
    let target = match prompt_target {
        PromptTarget::Browser => cstr!("webauthn-prompt"),
        PromptTarget::AboutPage => cstr!("about-webauthn-prompt"),
    };

    RunnableBuilder::new("AuthrsService::send_prompt", move || {
        if let Ok(obs_svc) = xpcom::components::Observer::service::<nsIObserverService>() {
            unsafe {
                obs_svc.NotifyObservers(std::ptr::null(), target.as_ptr(), json.as_ptr());
            }
        }
    })
    .dispatch(main_thread.coerce())
}

fn send_prompt(
    prompt: BrowserPromptType,
    tid: u64,
    origin: Option<&str>,
    browsing_context_id: Option<u64>,
) -> Result<(), nsresult> {
    let mut json = nsString::new();
    write!(
        json,
        "{}",
        json!(&BrowserPromptMessage {
            prompt,
            tid,
            origin,
            browsing_context_id
        })
    )
    .or(Err(NS_ERROR_FAILURE))?;
    notify_observers(PromptTarget::Browser, json)
}

fn cancel_prompts(tid: u64) -> Result<(), nsresult> {
    send_prompt(BrowserPromptType::Cancel, tid, None, None)?;
    Ok(())
}

#[xpcom(implement(nsIWebAuthnRegisterResult), atomic)]
pub struct WebAuthnRegisterResult {
    // result is only borrowed mutably in `Anonymize`.
    result: RefCell<RegisterResult>,
}

impl WebAuthnRegisterResult {
    xpcom_method!(get_client_data_json => GetClientDataJSON() -> nsACString);
    fn get_client_data_json(&self) -> Result<nsCString, nsresult> {
        Err(NS_ERROR_NOT_AVAILABLE)
    }

    xpcom_method!(get_attestation_object => GetAttestationObject() -> ThinVec<u8>);
    fn get_attestation_object(&self) -> Result<ThinVec<u8>, nsresult> {
        let mut out = ThinVec::new();
        serde_cbor::to_writer(&mut out, &self.result.borrow().att_obj).or(Err(NS_ERROR_FAILURE))?;
        Ok(out)
    }

    xpcom_method!(get_credential_id => GetCredentialId() -> ThinVec<u8>);
    fn get_credential_id(&self) -> Result<ThinVec<u8>, nsresult> {
        let Some(credential_data) = &self.result.borrow().att_obj.auth_data.credential_data else {
            return Err(NS_ERROR_FAILURE);
        };
        Ok(credential_data.credential_id.as_slice().into())
    }

    xpcom_method!(get_transports => GetTransports() -> ThinVec<nsString>);
    fn get_transports(&self) -> Result<ThinVec<nsString>, nsresult> {
        // The list that we return here might be included in a future GetAssertion request as a
        // hint as to which transports to try. In production, we only support the "usb" transport.
        // In tests, the result is not very important, but we can at least return "internal" if
        // we're simulating platform attachment.
        if static_prefs::pref!("security.webauth.webauthn_enable_softtoken")
            && self.result.borrow().attachment == AuthenticatorAttachment::Platform
        {
            Ok(thin_vec![nsString::from("internal")])
        } else {
            Ok(thin_vec![nsString::from("usb")])
        }
    }

    xpcom_method!(get_hmac_create_secret => GetHmacCreateSecret() -> bool);
    fn get_hmac_create_secret(&self) -> Result<bool, nsresult> {
        let Some(hmac_create_secret) = self.result.borrow().extensions.hmac_create_secret else {
            return Err(NS_ERROR_NOT_AVAILABLE);
        };
        Ok(hmac_create_secret)
    }

    xpcom_method!(get_large_blob_supported => GetLargeBlobSupported() -> bool);
    fn get_large_blob_supported(&self) -> Result<bool, nsresult> {
        Err(NS_ERROR_NOT_AVAILABLE)
    }

    xpcom_method!(get_prf_enabled => GetPrfEnabled() -> bool);
    fn get_prf_enabled(&self) -> Result<bool, nsresult> {
        match self.result.borrow().extensions.prf {
            Some(AuthenticationExtensionsPRFOutputs {
                enabled: Some(prf_enabled),
                ..
            }) => Ok(prf_enabled),
            _ => Err(NS_ERROR_NOT_AVAILABLE),
        }
    }

    xpcom_method!(get_prf_results_first => GetPrfResultsFirst() -> ThinVec<u8>);
    fn get_prf_results_first(&self) -> Result<ThinVec<u8>, nsresult> {
        match &self.result.borrow().extensions.prf {
            Some(AuthenticationExtensionsPRFOutputs {
                results: Some(AuthenticationExtensionsPRFValues { first, .. }),
                ..
            }) => Ok(first.as_slice().into()),
            _ => Err(NS_ERROR_NOT_AVAILABLE),
        }
    }

    xpcom_method!(get_prf_results_second => GetPrfResultsSecond() -> ThinVec<u8>);
    fn get_prf_results_second(&self) -> Result<ThinVec<u8>, nsresult> {
        match &self.result.borrow().extensions.prf {
            Some(AuthenticationExtensionsPRFOutputs {
                results:
                    Some(AuthenticationExtensionsPRFValues {
                        second: Some(second),
                        ..
                    }),
                ..
            }) => Ok(second.as_slice().into()),
            _ => Err(NS_ERROR_NOT_AVAILABLE),
        }
    }

    xpcom_method!(get_cred_props_rk => GetCredPropsRk() -> bool);
    fn get_cred_props_rk(&self) -> Result<bool, nsresult> {
        let Some(cred_props) = &self.result.borrow().extensions.cred_props else {
            return Err(NS_ERROR_NOT_AVAILABLE);
        };
        Ok(cred_props.rk)
    }

    xpcom_method!(set_cred_props_rk => SetCredPropsRk(aCredPropsRk: bool));
    fn set_cred_props_rk(&self, _cred_props_rk: bool) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(get_authenticator_attachment => GetAuthenticatorAttachment() -> nsAString);
    fn get_authenticator_attachment(&self) -> Result<nsString, nsresult> {
        match self.result.borrow().attachment {
            AuthenticatorAttachment::CrossPlatform => Ok(nsString::from("cross-platform")),
            AuthenticatorAttachment::Platform => Ok(nsString::from("platform")),
            AuthenticatorAttachment::Unknown => Err(NS_ERROR_NOT_AVAILABLE),
        }
    }

    xpcom_method!(has_identifying_attestation => HasIdentifyingAttestation() -> bool);
    fn has_identifying_attestation(&self) -> Result<bool, nsresult> {
        if self.result.borrow().att_obj.att_stmt != AttestationStatement::None {
            return Ok(true);
        }
        if let Some(data) = &self.result.borrow().att_obj.auth_data.credential_data {
            return Ok(data.aaguid != AAGuid::default());
        }
        Ok(false)
    }

    xpcom_method!(anonymize => Anonymize());
    fn anonymize(&self) -> Result<nsresult, nsresult> {
        self.result.borrow_mut().att_obj.anonymize();
        Ok(NS_OK)
    }
}

#[xpcom(implement(nsIWebAuthnAttObj), atomic)]
pub struct WebAuthnAttObj {
    att_obj: AttestationObject,
}

impl WebAuthnAttObj {
    xpcom_method!(get_attestation_object => GetAttestationObject() -> ThinVec<u8>);
    fn get_attestation_object(&self) -> Result<ThinVec<u8>, nsresult> {
        let mut out = ThinVec::new();
        serde_cbor::to_writer(&mut out, &self.att_obj).or(Err(NS_ERROR_FAILURE))?;
        Ok(out)
    }

    xpcom_method!(get_authenticator_data => GetAuthenticatorData() -> ThinVec<u8>);
    fn get_authenticator_data(&self) -> Result<ThinVec<u8>, nsresult> {
        // TODO(https://github.com/mozilla/authenticator-rs/issues/302) use to_writer
        Ok(self.att_obj.auth_data.to_vec().into())
    }

    xpcom_method!(get_public_key => GetPublicKey() -> ThinVec<u8>);
    fn get_public_key(&self) -> Result<ThinVec<u8>, nsresult> {
        let Some(credential_data) = &self.att_obj.auth_data.credential_data else {
            return Err(NS_ERROR_FAILURE);
        };
        Ok(credential_data
            .credential_public_key
            .der_spki()
            .or(Err(NS_ERROR_NOT_AVAILABLE))?
            .into())
    }

    xpcom_method!(get_public_key_algorithm => GetPublicKeyAlgorithm() -> i32);
    fn get_public_key_algorithm(&self) -> Result<i32, nsresult> {
        let Some(credential_data) = &self.att_obj.auth_data.credential_data else {
            return Err(NS_ERROR_FAILURE);
        };
        // safe to cast to i32 by inspection of defined values
        Ok(credential_data.credential_public_key.alg as i32)
    }

    xpcom_method!(is_identifying => IsIdentifying() -> bool);
    fn is_identifying(&self) -> Result<bool, nsresult> {
        if self.att_obj.att_stmt != AttestationStatement::None {
            return Ok(true);
        }
        if let Some(data) = &self.att_obj.auth_data.credential_data {
            return Ok(data.aaguid != AAGuid::default());
        }
        Ok(false)
    }
}

#[xpcom(implement(nsIWebAuthnSignResult), atomic)]
pub struct WebAuthnSignResult {
    result: SignResult,
}

impl WebAuthnSignResult {
    xpcom_method!(get_client_data_json => GetClientDataJSON() -> nsACString);
    fn get_client_data_json(&self) -> Result<nsCString, nsresult> {
        Err(NS_ERROR_NOT_AVAILABLE)
    }

    xpcom_method!(get_credential_id => GetCredentialId() -> ThinVec<u8>);
    fn get_credential_id(&self) -> Result<ThinVec<u8>, nsresult> {
        let Some(cred) = &self.result.assertion.credentials else {
            return Err(NS_ERROR_FAILURE);
        };
        Ok(cred.id.as_slice().into())
    }

    xpcom_method!(get_signature => GetSignature() -> ThinVec<u8>);
    fn get_signature(&self) -> Result<ThinVec<u8>, nsresult> {
        Ok(self.result.assertion.signature.as_slice().into())
    }

    xpcom_method!(get_authenticator_data => GetAuthenticatorData() -> ThinVec<u8>);
    fn get_authenticator_data(&self) -> Result<ThinVec<u8>, nsresult> {
        Ok(self.result.assertion.auth_data.to_vec().into())
    }

    xpcom_method!(get_user_handle => GetUserHandle() -> ThinVec<u8>);
    fn get_user_handle(&self) -> Result<ThinVec<u8>, nsresult> {
        let Some(user) = &self.result.assertion.user else {
            return Err(NS_ERROR_NOT_AVAILABLE);
        };
        Ok(user.id.as_slice().into())
    }

    xpcom_method!(get_user_name => GetUserName() -> nsACString);
    fn get_user_name(&self) -> Result<nsCString, nsresult> {
        let Some(user) = &self.result.assertion.user else {
            return Err(NS_ERROR_NOT_AVAILABLE);
        };
        let Some(name) = &user.name else {
            return Err(NS_ERROR_NOT_AVAILABLE);
        };
        Ok(nsCString::from(name))
    }

    xpcom_method!(get_authenticator_attachment => GetAuthenticatorAttachment() -> nsAString);
    fn get_authenticator_attachment(&self) -> Result<nsString, nsresult> {
        match self.result.attachment {
            AuthenticatorAttachment::CrossPlatform => Ok(nsString::from("cross-platform")),
            AuthenticatorAttachment::Platform => Ok(nsString::from("platform")),
            AuthenticatorAttachment::Unknown => Err(NS_ERROR_NOT_AVAILABLE),
        }
    }

    xpcom_method!(get_used_app_id => GetUsedAppId() -> bool);
    fn get_used_app_id(&self) -> Result<bool, nsresult> {
        self.result.extensions.app_id.ok_or(NS_ERROR_NOT_AVAILABLE)
    }

    xpcom_method!(set_used_app_id => SetUsedAppId(aUsedAppId: bool));
    fn set_used_app_id(&self, _used_app_id: bool) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(get_large_blob_value => GetLargeBlobValue() -> ThinVec<u8>);
    fn get_large_blob_value(&self) -> Result<ThinVec<u8>, nsresult> {
        Err(NS_ERROR_NOT_AVAILABLE)
    }

    xpcom_method!(get_large_blob_written => GetLargeBlobWritten() -> bool);
    fn get_large_blob_written(&self) -> Result<bool, nsresult> {
        Err(NS_ERROR_NOT_AVAILABLE)
    }

    xpcom_method!(get_prf_maybe => GetPrfMaybe() -> bool);
    /// Return true if a PRF output is present, even if all attributes are absent.
    fn get_prf_maybe(&self) -> Result<bool, nsresult> {
        Ok(self.result.extensions.prf.is_some())
    }

    xpcom_method!(get_prf_results_first => GetPrfResultsFirst() -> ThinVec<u8>);
    fn get_prf_results_first(&self) -> Result<ThinVec<u8>, nsresult> {
        match &self.result.extensions.prf {
            Some(AuthenticationExtensionsPRFOutputs {
                results: Some(AuthenticationExtensionsPRFValues { first, .. }),
                ..
            }) => Ok(first.as_slice().into()),
            _ => Err(NS_ERROR_NOT_AVAILABLE),
        }
    }

    xpcom_method!(get_prf_results_second => GetPrfResultsSecond() -> ThinVec<u8>);
    fn get_prf_results_second(&self) -> Result<ThinVec<u8>, nsresult> {
        match &self.result.extensions.prf {
            Some(AuthenticationExtensionsPRFOutputs {
                results:
                    Some(AuthenticationExtensionsPRFValues {
                        second: Some(second),
                        ..
                    }),
                ..
            }) => Ok(second.as_slice().into()),
            _ => Err(NS_ERROR_NOT_AVAILABLE),
        }
    }
}

// A transaction may create a channel to ask a user for additional input, e.g. a PIN. The Sender
// component of this channel is sent to an AuthrsServide in a StatusUpdate. AuthrsService
// caches the sender along with the expected (u64) transaction ID, which is used as a consistency
// check in callbacks.
type PinReceiver = Option<(u64, Sender<Pin>)>;
type SelectionReceiver = Option<(u64, Sender<Option<usize>>)>;

fn status_callback(
    status_rx: Receiver<StatusUpdate>,
    tid: u64,
    origin: &String,
    browsing_context_id: u64,
    transaction: Arc<Mutex<Option<TransactionState>>>, /* Shared with an AuthrsService */
) -> Result<(), nsresult> {
    let origin = Some(origin.as_str());
    let browsing_context_id = Some(browsing_context_id);
    loop {
        match status_rx.recv() {
            Ok(StatusUpdate::SelectDeviceNotice) => {
                debug!("STATUS: Please select a device by touching one of them.");
                send_prompt(
                    BrowserPromptType::SelectDevice,
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PresenceRequired) => {
                debug!("STATUS: Waiting for user presence");
                send_prompt(
                    BrowserPromptType::Presence,
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::PinRequired(sender))) => {
                let mut guard = transaction.lock().unwrap();
                let Some(transaction) = guard.as_mut() else {
                    warn!("STATUS: received status update after end of transaction.");
                    break;
                };
                transaction.pin_receiver.replace((tid, sender));
                send_prompt(
                    BrowserPromptType::PinRequired,
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::InvalidPin(sender, retries))) => {
                let mut guard = transaction.lock().unwrap();
                let Some(transaction) = guard.as_mut() else {
                    warn!("STATUS: received status update after end of transaction.");
                    break;
                };
                transaction.pin_receiver.replace((tid, sender));
                send_prompt(
                    BrowserPromptType::PinInvalid { retries },
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::PinAuthBlocked)) => {
                send_prompt(
                    BrowserPromptType::PinAuthBlocked,
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::PinBlocked)) => {
                send_prompt(
                    BrowserPromptType::DeviceBlocked,
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::PinNotSet)) => {
                send_prompt(
                    BrowserPromptType::PinNotSet,
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::InvalidUv(retries))) => {
                send_prompt(
                    BrowserPromptType::UvInvalid { retries },
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::UvBlocked)) => {
                send_prompt(
                    BrowserPromptType::UvBlocked,
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Ok(StatusUpdate::PinUvError(StatusPinUv::PinIsTooShort))
            | Ok(StatusUpdate::PinUvError(StatusPinUv::PinIsTooLong(..))) => {
                // These should never happen.
                warn!("STATUS: Got unexpected StatusPinUv-error.");
            }
            Ok(StatusUpdate::InteractiveManagement(_)) => {
                debug!("STATUS: interactive management");
            }
            Ok(StatusUpdate::SelectResultNotice(sender, entities)) => {
                debug!("STATUS: select result notice");
                let mut guard = transaction.lock().unwrap();
                let Some(transaction) = guard.as_mut() else {
                    warn!("STATUS: received status update after end of transaction.");
                    break;
                };
                transaction.selection_receiver.replace((tid, sender));
                send_prompt(
                    BrowserPromptType::SelectSignResult {
                        entities: &entities,
                    },
                    tid,
                    origin,
                    browsing_context_id,
                )?;
            }
            Err(RecvError) => {
                debug!("STATUS: end");
                break;
            }
        }
    }
    Ok(())
}

#[derive(Clone)]
struct RegisterPromise(RefPtr<nsIWebAuthnRegisterPromise>);

impl RegisterPromise {
    fn resolve_or_reject(&self, result: Result<RegisterResult, nsresult>) -> Result<(), nsresult> {
        match result {
            Ok(result) => {
                let wrapped_result = WebAuthnRegisterResult::allocate(InitWebAuthnRegisterResult {
                    result: RefCell::new(result),
                })
                .query_interface::<nsIWebAuthnRegisterResult>()
                .ok_or(NS_ERROR_FAILURE)?;
                unsafe { self.0.Resolve(wrapped_result.coerce()) };
            }
            Err(result) => {
                unsafe { self.0.Reject(result) };
            }
        }
        Ok(())
    }
}

#[derive(Clone)]
struct SignPromise(RefPtr<nsIWebAuthnSignPromise>);

impl SignPromise {
    fn resolve_or_reject(&self, result: Result<SignResult, nsresult>) -> Result<(), nsresult> {
        match result {
            Ok(result) => {
                let wrapped_result =
                    WebAuthnSignResult::allocate(InitWebAuthnSignResult { result })
                        .query_interface::<nsIWebAuthnSignResult>()
                        .ok_or(NS_ERROR_FAILURE)?;
                unsafe { self.0.Resolve(wrapped_result.coerce()) };
            }
            Err(result) => {
                unsafe { self.0.Reject(result) };
            }
        }
        Ok(())
    }
}

#[derive(Clone)]
enum TransactionPromise {
    Listen,
    Register(RegisterPromise),
    Sign(SignPromise),
}

impl TransactionPromise {
    fn reject(&self, err: nsresult) -> Result<(), nsresult> {
        match self {
            TransactionPromise::Listen => Ok(()),
            TransactionPromise::Register(promise) => promise.resolve_or_reject(Err(err)),
            TransactionPromise::Sign(promise) => promise.resolve_or_reject(Err(err)),
        }
    }
}

enum TransactionArgs {
    Sign(/* timeout */ u64, SignArgs),
}

struct TransactionState {
    tid: u64,
    browsing_context_id: u64,
    pending_args: Option<TransactionArgs>,
    promise: TransactionPromise,
    pin_receiver: PinReceiver,
    selection_receiver: SelectionReceiver,
    interactive_receiver: InteractiveManagementReceiver,
    puat_cache: Option<PinUvAuthResult>, // Cached credential to avoid repeated PIN-entries
}

// AuthrsService provides an nsIWebAuthnService built on top of authenticator-rs.
#[xpcom(implement(nsIWebAuthnService), atomic)]
pub struct AuthrsService {
    usb_token_manager: Mutex<StateMachine>,
    test_token_manager: TestTokenManager,
    transaction: Arc<Mutex<Option<TransactionState>>>,
}

impl AuthrsService {
    xpcom_method!(pin_callback => PinCallback(aTransactionId: u64, aPin: *const nsACString));
    fn pin_callback(&self, transaction_id: u64, pin: &nsACString) -> Result<(), nsresult> {
        if static_prefs::pref!("security.webauth.webauthn_enable_usbtoken") {
            let mut guard = self.transaction.lock().unwrap();
            let Some(transaction) = guard.as_mut() else {
                // No ongoing transaction
                return Err(NS_ERROR_FAILURE);
            };
            let Some((tid, channel)) = transaction.pin_receiver.take() else {
                // We weren't expecting a pin.
                return Err(NS_ERROR_FAILURE);
            };
            if tid != transaction_id {
                // The browser is confused about which transaction is active.
                // This shouldn't happen
                return Err(NS_ERROR_FAILURE);
            }
            channel
                .send(Pin::new(&pin.to_string()))
                .or(Err(NS_ERROR_FAILURE))
        } else {
            // Silently accept request, if all webauthn-options are disabled.
            // Used for testing.
            Ok(())
        }
    }

    xpcom_method!(selection_callback => SelectionCallback(aTransactionId: u64, aSelection: u64));
    fn selection_callback(&self, transaction_id: u64, selection: u64) -> Result<(), nsresult> {
        let mut guard = self.transaction.lock().unwrap();
        let Some(transaction) = guard.as_mut() else {
            // No ongoing transaction
            return Err(NS_ERROR_FAILURE);
        };
        let Some((tid, channel)) = transaction.selection_receiver.take() else {
            // We weren't expecting a selection.
            return Err(NS_ERROR_FAILURE);
        };
        if tid != transaction_id {
            // The browser is confused about which transaction is active.
            // This shouldn't happen
            return Err(NS_ERROR_FAILURE);
        }
        channel
            .send(Some(selection as usize))
            .or(Err(NS_ERROR_FAILURE))
    }

    xpcom_method!(get_is_uvpaa => GetIsUVPAA() -> bool);
    fn get_is_uvpaa(&self) -> Result<bool, nsresult> {
        if static_prefs::pref!("security.webauth.webauthn_enable_usbtoken") {
            Ok(false)
        } else if static_prefs::pref!("security.webauth.webauthn_enable_softtoken") {
            Ok(self.test_token_manager.has_platform_authenticator())
        } else {
            Err(NS_ERROR_NOT_AVAILABLE)
        }
    }

    xpcom_method!(make_credential => MakeCredential(aTid: u64, aBrowsingContextId: u64, aArgs: *const nsIWebAuthnRegisterArgs, aPromise: *const nsIWebAuthnRegisterPromise));
    fn make_credential(
        &self,
        tid: u64,
        browsing_context_id: u64,
        args: &nsIWebAuthnRegisterArgs,
        promise: &nsIWebAuthnRegisterPromise,
    ) -> Result<(), nsresult> {
        self.reset()?;

        let promise = RegisterPromise(RefPtr::new(promise));

        let mut origin = nsString::new();
        unsafe { args.GetOrigin(&mut *origin) }.to_result()?;

        let mut relying_party_id = nsString::new();
        unsafe { args.GetRpId(&mut *relying_party_id) }.to_result()?;

        let mut client_data_hash = ThinVec::new();
        unsafe { args.GetClientDataHash(&mut client_data_hash) }.to_result()?;
        let mut client_data_hash_arr = [0u8; 32];
        client_data_hash_arr.copy_from_slice(&client_data_hash);

        let mut timeout_ms = 0u32;
        unsafe { args.GetTimeoutMS(&mut timeout_ms) }.to_result()?;

        let mut exclude_list = ThinVec::new();
        unsafe { args.GetExcludeList(&mut exclude_list) }.to_result()?;
        let exclude_list = exclude_list
            .iter_mut()
            .map(|id| PublicKeyCredentialDescriptor {
                id: id.to_vec(),
                transports: vec![],
            })
            .collect();

        let mut relying_party_name = nsString::new();
        unsafe { args.GetRpName(&mut *relying_party_name) }.to_result()?;

        let mut user_id = ThinVec::new();
        unsafe { args.GetUserId(&mut user_id) }.to_result()?;

        let mut user_name = nsString::new();
        unsafe { args.GetUserName(&mut *user_name) }.to_result()?;

        let mut user_display_name = nsString::new();
        unsafe { args.GetUserDisplayName(&mut *user_display_name) }.to_result()?;

        let mut cose_algs = ThinVec::new();
        unsafe { args.GetCoseAlgs(&mut cose_algs) }.to_result()?;
        let pub_cred_params = cose_algs
            .iter()
            .filter_map(|alg| PublicKeyCredentialParameters::try_from(*alg).ok())
            .collect();

        let mut resident_key = nsString::new();
        unsafe { args.GetResidentKey(&mut *resident_key) }.to_result()?;
        let resident_key_req = if resident_key.eq("required") {
            ResidentKeyRequirement::Required
        } else if resident_key.eq("preferred") {
            ResidentKeyRequirement::Preferred
        } else if resident_key.eq("discouraged") {
            ResidentKeyRequirement::Discouraged
        } else {
            return Err(NS_ERROR_FAILURE);
        };

        let mut user_verification = nsString::new();
        unsafe { args.GetUserVerification(&mut *user_verification) }.to_result()?;
        let user_verification_req = if user_verification.eq("required") {
            UserVerificationRequirement::Required
        } else if user_verification.eq("discouraged") {
            UserVerificationRequirement::Discouraged
        } else {
            UserVerificationRequirement::Preferred
        };

        let mut authenticator_attachment = nsString::new();
        if unsafe { args.GetAuthenticatorAttachment(&mut *authenticator_attachment) }
            .to_result()
            .is_ok()
        {
            if authenticator_attachment.eq("platform") {
                return Err(NS_ERROR_FAILURE);
            }
        }

        let mut credential_protection_policy = None;
        let mut enforce_credential_protection_policy = None;
        let mut cred_protect_policy_value = nsCString::new();
        let mut enforce_cred_protect_value = false;
        if unsafe { args.GetCredentialProtectionPolicy(&mut *cred_protect_policy_value) }
            .to_result()
            .is_ok()
        {
            unsafe { args.GetEnforceCredentialProtectionPolicy(&mut enforce_cred_protect_value) }
                .to_result()?;
            credential_protection_policy = if cred_protect_policy_value
                .eq("userVerificationOptional")
            {
                Some(CredentialProtectionPolicy::UserVerificationOptional)
            } else if cred_protect_policy_value.eq("userVerificationOptionalWithCredentialIDList") {
                Some(CredentialProtectionPolicy::UserVerificationOptionalWithCredentialIDList)
            } else if cred_protect_policy_value.eq("userVerificationRequired") {
                Some(CredentialProtectionPolicy::UserVerificationRequired)
            } else {
                return Err(NS_ERROR_FAILURE);
            };
            enforce_credential_protection_policy = Some(enforce_cred_protect_value);
        }

        let mut cred_props = false;
        unsafe { args.GetCredProps(&mut cred_props) }.to_result()?;

        let mut min_pin_length = false;
        unsafe { args.GetMinPinLength(&mut min_pin_length) }.to_result()?;

        let prf_input = (|| -> Option<AuthenticationExtensionsPRFInputs> {
            let mut prf: bool = false;
            unsafe { args.GetPrf(&mut prf) }.to_result().ok()?;
            if !prf {
                return None;
            }

            let eval = || -> Option<AuthenticationExtensionsPRFValues> {
                let mut prf_eval_first: ThinVec<u8> = ThinVec::new();
                let mut prf_eval_second: ThinVec<u8> = ThinVec::new();
                unsafe { args.GetPrfEvalFirst(&mut prf_eval_first) }
                    .to_result()
                    .ok()?;
                let has_second = unsafe { args.GetPrfEvalSecond(&mut prf_eval_second) }
                    .to_result()
                    .is_ok();
                Some(AuthenticationExtensionsPRFValues {
                    first: prf_eval_first.to_vec(),
                    second: has_second.then(|| prf_eval_second.to_vec()),
                })
            }();

            Some(AuthenticationExtensionsPRFInputs {
                eval,
                eval_by_credential: None,
            })
        })();

        let mut hmac_create_secret = None;
        let mut maybe_hmac_create_secret = false;
        if unsafe { args.GetHmacCreateSecret(&mut maybe_hmac_create_secret) }
            .to_result()
            .is_ok()
        {
            hmac_create_secret = Some(maybe_hmac_create_secret);
        }

        let origin = origin.to_string();
        let info = RegisterArgs {
            client_data_hash: client_data_hash_arr,
            relying_party: RelyingParty {
                id: relying_party_id.to_string(),
                name: None,
            },
            origin: origin.clone(),
            user: PublicKeyCredentialUserEntity {
                id: user_id.to_vec(),
                name: Some(user_name.to_string()),
                display_name: None,
            },
            pub_cred_params,
            exclude_list,
            user_verification_req,
            resident_key_req,
            extensions: AuthenticationExtensionsClientInputs {
                credential_protection_policy,
                enforce_credential_protection_policy,
                cred_props: cred_props.then_some(true),
                hmac_create_secret,
                min_pin_length: min_pin_length.then_some(true),
                prf: prf_input,
                ..Default::default()
            },
            pin: None,
            use_ctap1_fallback: !static_prefs::pref!("security.webauthn.ctap2"),
        };

        let mut guard = self.transaction.lock().unwrap();
        *guard = Some(TransactionState {
            tid,
            browsing_context_id,
            pending_args: None,
            promise: TransactionPromise::Register(promise),
            pin_receiver: None,
            selection_receiver: None,
            interactive_receiver: None,
            puat_cache: None,
        });
        // drop the guard here to ensure we don't deadlock if the call to `register()` below
        // hairpins the state callback.
        drop(guard);

        let (status_tx, status_rx) = channel::<StatusUpdate>();
        let status_transaction = self.transaction.clone();
        let status_origin = info.origin.clone();
        RunnableBuilder::new("AuthrsService::MakeCredential::StatusReceiver", move || {
            let _ = status_callback(
                status_rx,
                tid,
                &status_origin,
                browsing_context_id,
                status_transaction,
            );
        })
        .may_block(true)
        .dispatch_background_task()?;

        let callback_transaction = self.transaction.clone();
        let callback_origin = info.origin.clone();
        let state_callback = StateCallback::<Result<RegisterResult, AuthenticatorError>>::new(
            Box::new(move |result| {
                let mut guard = callback_transaction.lock().unwrap();
                let Some(state) = guard.as_mut() else {
                    return;
                };
                if state.tid != tid {
                    return;
                }
                let TransactionPromise::Register(ref promise) = state.promise else {
                    return;
                };
                if let Err(AuthenticatorError::CredentialExcluded) = result {
                    let _ = send_prompt(
                        BrowserPromptType::AlreadyRegistered,
                        tid,
                        Some(&callback_origin),
                        Some(browsing_context_id),
                    );
                }
                if should_cancel_prompts(&result) {
                    // Some errors are accompanied by prompts that should persist after the
                    // operation terminates.
                    let _ = cancel_prompts(tid);
                }
                let _ = promise.resolve_or_reject(result.map_err(authrs_to_nserror));
                *guard = None;
            }),
        );

        // The authenticator crate provides an `AuthenticatorService` which can dispatch a request
        // in parallel to any number of transports. We only support the USB transport in production
        // configurations, so we do not need the full generality of `AuthenticatorService` here.
        // We disable the USB transport in tests that use virtual devices.
        if static_prefs::pref!("security.webauth.webauthn_enable_usbtoken") {
            // TODO(Bug 1855290) Remove this presence prompt
            send_prompt(
                BrowserPromptType::Presence,
                tid,
                Some(&info.origin),
                Some(browsing_context_id),
            )?;
            self.usb_token_manager.lock().unwrap().register(
                timeout_ms.into(),
                info,
                status_tx,
                state_callback,
            );
        } else if static_prefs::pref!("security.webauth.webauthn_enable_softtoken") {
            self.test_token_manager
                .register(timeout_ms.into(), info, status_tx, state_callback);
        } else {
            return Err(NS_ERROR_FAILURE);
        }

        Ok(())
    }

    xpcom_method!(set_has_attestation_consent => SetHasAttestationConsent(aTid: u64, aHasConsent: bool));
    fn set_has_attestation_consent(&self, _tid: u64, _has_consent: bool) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(get_assertion => GetAssertion(aTid: u64, aBrowsingContextId: u64, aArgs: *const nsIWebAuthnSignArgs, aPromise: *const nsIWebAuthnSignPromise));
    fn get_assertion(
        &self,
        tid: u64,
        browsing_context_id: u64,
        args: &nsIWebAuthnSignArgs,
        promise: &nsIWebAuthnSignPromise,
    ) -> Result<(), nsresult> {
        self.reset()?;

        let promise = SignPromise(RefPtr::new(promise));

        let mut origin = nsString::new();
        unsafe { args.GetOrigin(&mut *origin) }.to_result()?;

        let mut relying_party_id = nsString::new();
        unsafe { args.GetRpId(&mut *relying_party_id) }.to_result()?;

        let mut client_data_hash = ThinVec::new();
        unsafe { args.GetClientDataHash(&mut client_data_hash) }.to_result()?;
        let mut client_data_hash_arr = [0u8; 32];
        client_data_hash_arr.copy_from_slice(&client_data_hash);

        let mut timeout_ms = 0u32;
        unsafe { args.GetTimeoutMS(&mut timeout_ms) }.to_result()?;

        let mut allow_list = ThinVec::new();
        unsafe { args.GetAllowList(&mut allow_list) }.to_result()?;
        let allow_list = allow_list
            .iter()
            .map(|id| PublicKeyCredentialDescriptor {
                id: id.to_vec(),
                transports: vec![],
            })
            .collect();

        let mut user_verification = nsString::new();
        unsafe { args.GetUserVerification(&mut *user_verification) }.to_result()?;
        let mut user_verification_req = if user_verification.eq("required") {
            UserVerificationRequirement::Required
        } else if user_verification.eq("discouraged") {
            UserVerificationRequirement::Discouraged
        } else {
            UserVerificationRequirement::Preferred
        };

        let mut app_id = None;
        let mut maybe_app_id = nsString::new();
        match unsafe { args.GetAppId(&mut *maybe_app_id) }.to_result() {
            Ok(_) => app_id = Some(maybe_app_id.to_string()),
            _ => (),
        }

        let prf_input = || -> Option<AuthenticationExtensionsPRFInputs> {
            let mut prf: bool = false;
            unsafe { args.GetPrf(&mut prf) }.to_result().ok()?;
            if !prf {
                return None;
            }

            let eval = || -> Option<AuthenticationExtensionsPRFValues> {
                let mut prf_eval_first: ThinVec<u8> = ThinVec::new();
                let mut prf_eval_second: ThinVec<u8> = ThinVec::new();
                unsafe { args.GetPrfEvalFirst(&mut prf_eval_first) }
                    .to_result()
                    .ok()?;
                let has_second = unsafe { args.GetPrfEvalSecond(&mut prf_eval_second) }
                    .to_result()
                    .is_ok();
                Some(AuthenticationExtensionsPRFValues {
                    first: prf_eval_first.to_vec(),
                    second: has_second.then(|| prf_eval_second.to_vec()),
                })
            }();

            let eval_by_credential =
                || -> Option<HashMap<Vec<u8>, AuthenticationExtensionsPRFValues>> {
                    let mut credential_ids: ThinVec<ThinVec<u8>> = ThinVec::new();
                    let mut eval_by_cred_firsts: ThinVec<ThinVec<u8>> = ThinVec::new();
                    let mut eval_by_cred_second_maybes: ThinVec<bool> = ThinVec::new();
                    let mut eval_by_cred_seconds: ThinVec<ThinVec<u8>> = ThinVec::new();
                    unsafe { args.GetPrfEvalByCredentialCredentialId(&mut credential_ids) }
                        .to_result()
                        .ok()?;
                    unsafe { args.GetPrfEvalByCredentialEvalFirst(&mut eval_by_cred_firsts) }
                        .to_result()
                        .ok()?;
                    unsafe {
                        args.GetPrfEvalByCredentialEvalSecondMaybe(&mut eval_by_cred_second_maybes)
                    }
                    .to_result()
                    .ok()?;
                    unsafe { args.GetPrfEvalByCredentialEvalSecond(&mut eval_by_cred_seconds) }
                        .to_result()
                        .ok()?;
                    if credential_ids.len() != eval_by_cred_firsts.len()
                        || credential_ids.len() != eval_by_cred_second_maybes.len()
                        || credential_ids.len() != eval_by_cred_seconds.len()
                    {
                        return None;
                    }
                    let mut result = HashMap::new();
                    for i in 0..credential_ids.len() {
                        result.insert(
                            credential_ids[i].to_vec(),
                            AuthenticationExtensionsPRFValues {
                                first: eval_by_cred_firsts[i].to_vec(),
                                second: eval_by_cred_second_maybes[i]
                                    .then(|| eval_by_cred_seconds[i].to_vec()),
                            },
                        );
                    }
                    Some(result)
                }();

            Some(AuthenticationExtensionsPRFInputs {
                eval,
                eval_by_credential,
            })
        }();

        // https://w3c.github.io/webauthn/#prf-extension
        // "The hmac-secret extension provides two PRFs per credential: one which is used for
        // requests where user verification is performed and another for all other requests.
        // This extension [PRF] only exposes a single PRF per credential and, when implementing
        // on top of hmac-secret, that PRF MUST be the one used for when user verification is
        // performed. This overrides the UserVerificationRequirement if neccessary."
        if prf_input.is_some() && user_verification_req == UserVerificationRequirement::Discouraged
        {
            user_verification_req = UserVerificationRequirement::Preferred;
        }

        let mut conditionally_mediated = false;
        unsafe { args.GetConditionallyMediated(&mut conditionally_mediated) }.to_result()?;

        let info = SignArgs {
            client_data_hash: client_data_hash_arr,
            relying_party_id: relying_party_id.to_string(),
            origin: origin.to_string(),
            allow_list,
            user_verification_req,
            user_presence_req: true,
            extensions: AuthenticationExtensionsClientInputs {
                app_id,
                prf: prf_input,
                ..Default::default()
            },
            pin: None,
            use_ctap1_fallback: !static_prefs::pref!("security.webauthn.ctap2"),
        };

        let mut guard = self.transaction.lock().unwrap();
        *guard = Some(TransactionState {
            tid,
            browsing_context_id,
            pending_args: Some(TransactionArgs::Sign(timeout_ms as u64, info)),
            promise: TransactionPromise::Sign(promise),
            pin_receiver: None,
            selection_receiver: None,
            interactive_receiver: None,
            puat_cache: None,
        });

        if !conditionally_mediated {
            // Immediately proceed to the modal UI flow.
            self.do_get_assertion(None, guard)
        } else {
            // Cache the request and wait for the conditional UI to request autofill entries, etc.
            Ok(())
        }
    }

    fn do_get_assertion(
        &self,
        mut selected_credential_id: Option<Vec<u8>>,
        mut guard: MutexGuard<Option<TransactionState>>,
    ) -> Result<(), nsresult> {
        let Some(state) = guard.as_mut() else {
            return Err(NS_ERROR_FAILURE);
        };
        let browsing_context_id = state.browsing_context_id;
        let tid = state.tid;
        let (timeout_ms, mut info) = match state.pending_args.take() {
            Some(TransactionArgs::Sign(timeout_ms, info)) => (timeout_ms, info),
            _ => return Err(NS_ERROR_FAILURE),
        };

        if let Some(id) = selected_credential_id.take() {
            if info.allow_list.is_empty() {
                info.allow_list.push(PublicKeyCredentialDescriptor {
                    id,
                    transports: vec![],
                });
            } else {
                // We need to ensure that the selected credential id
                // was in the original allow_list.
                info.allow_list.retain(|cred| cred.id == id);
                if info.allow_list.is_empty() {
                    return Err(NS_ERROR_FAILURE);
                }
            }
        }

        let (status_tx, status_rx) = channel::<StatusUpdate>();
        let status_transaction = self.transaction.clone();
        let status_origin = info.origin.to_string();
        RunnableBuilder::new("AuthrsService::GetAssertion::StatusReceiver", move || {
            let _ = status_callback(
                status_rx,
                tid,
                &status_origin,
                browsing_context_id,
                status_transaction,
            );
        })
        .may_block(true)
        .dispatch_background_task()?;

        let uniq_allowed_cred = if info.allow_list.len() == 1 {
            info.allow_list.first().cloned()
        } else {
            None
        };

        let callback_transaction = self.transaction.clone();
        let state_callback = StateCallback::<Result<SignResult, AuthenticatorError>>::new(
            Box::new(move |mut result| {
                let mut guard = callback_transaction.lock().unwrap();
                let Some(state) = guard.as_mut() else {
                    return;
                };
                if state.tid != tid {
                    return;
                }
                let TransactionPromise::Sign(ref promise) = state.promise else {
                    return;
                };
                if uniq_allowed_cred.is_some() {
                    // In CTAP 2.0, but not CTAP 2.1, the assertion object's credential field
                    // "May be omitted if the allowList has exactly one credential." If we had
                    // a unique allowed credential, then copy its descriptor to the output.
                    if let Ok(inner) = result.as_mut() {
                        inner.assertion.credentials = uniq_allowed_cred;
                    }
                }
                if should_cancel_prompts(&result) {
                    // Some errors are accompanied by prompts that should persist after the
                    // operation terminates.
                    let _ = cancel_prompts(tid);
                }
                let _ = promise.resolve_or_reject(result.map_err(authrs_to_nserror));
                *guard = None;
            }),
        );

        // TODO(Bug 1855290) Remove this presence prompt
        send_prompt(
            BrowserPromptType::Presence,
            tid,
            Some(&info.origin),
            Some(browsing_context_id),
        )?;

        // As in `register`, we are intentionally avoiding `AuthenticatorService` here.
        if static_prefs::pref!("security.webauth.webauthn_enable_usbtoken") {
            self.usb_token_manager.lock().unwrap().sign(
                timeout_ms as u64,
                info,
                status_tx,
                state_callback,
            );
        } else if static_prefs::pref!("security.webauth.webauthn_enable_softtoken") {
            self.test_token_manager
                .sign(timeout_ms as u64, info, status_tx, state_callback);
        } else {
            return Err(NS_ERROR_FAILURE);
        }

        Ok(())
    }

    xpcom_method!(has_pending_conditional_get => HasPendingConditionalGet(aBrowsingContextId: u64, aOrigin: *const nsAString) -> u64);
    fn has_pending_conditional_get(
        &self,
        browsing_context_id: u64,
        origin: &nsAString,
    ) -> Result<u64, nsresult> {
        let mut guard = self.transaction.lock().unwrap();
        let Some(state) = guard.as_mut() else {
            return Ok(0);
        };
        let Some(TransactionArgs::Sign(_, info)) = state.pending_args.as_ref() else {
            return Ok(0);
        };
        if state.browsing_context_id != browsing_context_id {
            return Ok(0);
        }
        if !info.origin.eq(&origin.to_string()) {
            return Ok(0);
        }
        Ok(state.tid)
    }

    xpcom_method!(get_autofill_entries => GetAutoFillEntries(aTransactionId: u64) -> ThinVec<Option<RefPtr<nsIWebAuthnAutoFillEntry>>>);
    fn get_autofill_entries(
        &self,
        tid: u64,
    ) -> Result<ThinVec<Option<RefPtr<nsIWebAuthnAutoFillEntry>>>, nsresult> {
        let mut guard = self.transaction.lock().unwrap();
        let Some(state) = guard.as_mut() else {
            return Err(NS_ERROR_NOT_AVAILABLE);
        };
        if state.tid != tid {
            return Err(NS_ERROR_NOT_AVAILABLE);
        }
        let Some(TransactionArgs::Sign(_, info)) = state.pending_args.as_ref() else {
            return Err(NS_ERROR_NOT_AVAILABLE);
        };
        if static_prefs::pref!("security.webauth.webauthn_enable_usbtoken") {
            // We don't currently support silent discovery for credentials on USB tokens.
            return Ok(thin_vec![]);
        } else if static_prefs::pref!("security.webauth.webauthn_enable_softtoken") {
            return self
                .test_token_manager
                .get_autofill_entries(&info.relying_party_id, &info.allow_list);
        } else {
            return Err(NS_ERROR_FAILURE);
        }
    }

    xpcom_method!(select_autofill_entry => SelectAutoFillEntry(aTid: u64, aCredentialId: *const ThinVec<u8>));
    fn select_autofill_entry(&self, tid: u64, credential_id: &ThinVec<u8>) -> Result<(), nsresult> {
        let mut guard = self.transaction.lock().unwrap();
        let Some(state) = guard.as_mut() else {
            return Err(NS_ERROR_FAILURE);
        };
        if tid != state.tid {
            return Err(NS_ERROR_FAILURE);
        }
        self.do_get_assertion(Some(credential_id.to_vec()), guard)
    }

    xpcom_method!(resume_conditional_get => ResumeConditionalGet(aTid: u64));
    fn resume_conditional_get(&self, tid: u64) -> Result<(), nsresult> {
        let mut guard = self.transaction.lock().unwrap();
        let Some(state) = guard.as_mut() else {
            return Err(NS_ERROR_FAILURE);
        };
        if tid != state.tid {
            return Err(NS_ERROR_FAILURE);
        }
        self.do_get_assertion(None, guard)
    }

    // Clears the transaction state if tid matches the ongoing transaction ID.
    // Returns whether the tid was a match.
    fn clear_transaction(&self, tid: u64) -> bool {
        let mut guard = self.transaction.lock().unwrap();
        let Some(state) = guard.as_ref() else {
            return true; // workaround for Bug 1864526.
        };
        if state.tid != tid {
            // Ignore the cancellation request if the transaction
            // ID does not match.
            return false;
        }
        // It's possible that we haven't dispatched the request to the usb_token_manager yet,
        // e.g. if we're waiting for resume_make_credential. So reject the promise and drop the
        // state here rather than from the StateCallback
        let _ = state.promise.reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
        *guard = None;
        true
    }

    xpcom_method!(cancel => Cancel(aTransactionId: u64));
    fn cancel(&self, tid: u64) -> Result<(), nsresult> {
        if self.clear_transaction(tid) {
            self.usb_token_manager.lock().unwrap().cancel();
        }
        Ok(())
    }

    xpcom_method!(reset => Reset());
    fn reset(&self) -> Result<(), nsresult> {
        {
            if let Some(state) = self.transaction.lock().unwrap().take() {
                cancel_prompts(state.tid)?;
                state.promise.reject(NS_ERROR_DOM_ABORT_ERR)?;
            }
        } // release the transaction lock so a StateCallback can take it
        self.usb_token_manager.lock().unwrap().cancel();
        Ok(())
    }

    xpcom_method!(
        add_virtual_authenticator => AddVirtualAuthenticator(
            protocol: *const nsACString,
            transport: *const nsACString,
            hasResidentKey: bool,
            hasUserVerification: bool,
            isUserConsenting: bool,
            isUserVerified: bool) -> u64
    );
    fn add_virtual_authenticator(
        &self,
        protocol: &nsACString,
        transport: &nsACString,
        has_resident_key: bool,
        has_user_verification: bool,
        is_user_consenting: bool,
        is_user_verified: bool,
    ) -> Result<u64, nsresult> {
        let protocol = match protocol.to_string().as_str() {
            "ctap1/u2f" => AuthenticatorVersion::U2F_V2,
            "ctap2" => AuthenticatorVersion::FIDO_2_0,
            "ctap2_1" => AuthenticatorVersion::FIDO_2_1,
            _ => return Err(NS_ERROR_INVALID_ARG),
        };
        let transport = transport.to_string();
        match transport.as_str() {
            "usb" | "nfc" | "ble" | "smart-card" | "hybrid" | "internal" => (),
            _ => return Err(NS_ERROR_INVALID_ARG),
        };
        self.test_token_manager.add_virtual_authenticator(
            protocol,
            transport,
            has_resident_key,
            has_user_verification,
            is_user_consenting,
            is_user_verified,
        )
    }

    xpcom_method!(remove_virtual_authenticator => RemoveVirtualAuthenticator(authenticatorId: u64));
    fn remove_virtual_authenticator(&self, authenticator_id: u64) -> Result<(), nsresult> {
        self.test_token_manager
            .remove_virtual_authenticator(authenticator_id)
    }

    xpcom_method!(
        add_credential => AddCredential(
            authenticatorId: u64,
            credentialId: *const nsACString,
            isResidentCredential: bool,
            rpId: *const nsACString,
            privateKey: *const nsACString,
            userHandle: *const nsACString,
            signCount: u32)
    );
    fn add_credential(
        &self,
        authenticator_id: u64,
        credential_id: &nsACString,
        is_resident_credential: bool,
        rp_id: &nsACString,
        private_key: &nsACString,
        user_handle: &nsACString,
        sign_count: u32,
    ) -> Result<(), nsresult> {
        let credential_id = base64::engine::general_purpose::URL_SAFE_NO_PAD
            .decode(credential_id)
            .or(Err(NS_ERROR_INVALID_ARG))?;
        let private_key = base64::engine::general_purpose::URL_SAFE_NO_PAD
            .decode(private_key)
            .or(Err(NS_ERROR_INVALID_ARG))?;
        let user_handle = base64::engine::general_purpose::URL_SAFE_NO_PAD
            .decode(user_handle)
            .or(Err(NS_ERROR_INVALID_ARG))?;
        self.test_token_manager.add_credential(
            authenticator_id,
            &credential_id,
            &private_key,
            &user_handle,
            sign_count,
            rp_id.to_string(),
            is_resident_credential,
        )
    }

    xpcom_method!(get_credentials => GetCredentials(authenticatorId: u64) -> ThinVec<Option<RefPtr<nsICredentialParameters>>>);
    fn get_credentials(
        &self,
        authenticator_id: u64,
    ) -> Result<ThinVec<Option<RefPtr<nsICredentialParameters>>>, nsresult> {
        self.test_token_manager.get_credentials(authenticator_id)
    }

    xpcom_method!(remove_credential => RemoveCredential(authenticatorId: u64, credentialId: *const nsACString));
    fn remove_credential(
        &self,
        authenticator_id: u64,
        credential_id: &nsACString,
    ) -> Result<(), nsresult> {
        let credential_id = base64::engine::general_purpose::URL_SAFE_NO_PAD
            .decode(credential_id)
            .or(Err(NS_ERROR_INVALID_ARG))?;
        self.test_token_manager
            .remove_credential(authenticator_id, credential_id.as_ref())
    }

    xpcom_method!(remove_all_credentials => RemoveAllCredentials(authenticatorId: u64));
    fn remove_all_credentials(&self, authenticator_id: u64) -> Result<(), nsresult> {
        self.test_token_manager
            .remove_all_credentials(authenticator_id)
    }

    xpcom_method!(set_user_verified => SetUserVerified(authenticatorId: u64, isUserVerified: bool));
    fn set_user_verified(
        &self,
        authenticator_id: u64,
        is_user_verified: bool,
    ) -> Result<(), nsresult> {
        self.test_token_manager
            .set_user_verified(authenticator_id, is_user_verified)
    }

    xpcom_method!(listen => Listen());
    pub(crate) fn listen(&self) -> Result<(), nsresult> {
        // For now, we don't support softtokens
        if static_prefs::pref!("security.webauth.webauthn_enable_softtoken") {
            return Ok(());
        }

        {
            let mut guard = self.transaction.lock().unwrap();
            if guard.as_ref().is_some() {
                // ignore listen() and continue with ongoing transaction
                return Ok(());
            }
            *guard = Some(TransactionState {
                tid: 0,
                browsing_context_id: 0,
                pending_args: None,
                promise: TransactionPromise::Listen,
                pin_receiver: None,
                selection_receiver: None,
                interactive_receiver: None,
                puat_cache: None,
            });
        }

        // We may get from status_updates info about certain errors (e.g. PinErrors)
        // which we want to present to the user. We will ignore the following error
        // which is caused by us "hanging up" on the StatusUpdate-channel and return
        // the PinError instead, via `upcoming_error`.
        let upcoming_error = Arc::new(Mutex::new(None));
        let upcoming_error_c = upcoming_error.clone();
        let callback_transaction = self.transaction.clone();
        let state_callback = StateCallback::<Result<ManageResult, AuthenticatorError>>::new(
            Box::new(move |result| {
                let mut guard = callback_transaction.lock().unwrap();
                match guard.as_mut() {
                    Some(state) => {
                        match state.promise {
                            TransactionPromise::Listen => (),
                            _ => return,
                        }
                        *guard = None;
                    }
                    // We have no transaction anymore, this means cancel() was called
                    None => (),
                }
                let msg = match result {
                    Ok(_) => BrowserPromptType::ListenSuccess,
                    Err(e) => {
                        // See if we have a cached error that should replace this error
                        let replacement = if let Ok(mut x) = upcoming_error_c.lock() {
                            x.take()
                        } else {
                            None
                        };
                        let replaced_err = replacement.unwrap_or(e);
                        let err = authrs_to_prompt(replaced_err);
                        BrowserPromptType::ListenError {
                            error: Box::new(err),
                        }
                    }
                };
                let _ = send_about_prompt(&msg);
            }),
        );

        // Calling `manage()` within the lock, to avoid race conditions
        // where we might check listen_blocked, see that it's false,
        // continue along, but in parallel `make_credential()` aborts the
        // interactive process shortly after, setting listen_blocked to true,
        // then accessing usb_token_manager afterwards and at the same time
        // we do it here, causing a runtime crash for trying to mut-borrow it twice.
        let (status_tx, status_rx) = channel::<StatusUpdate>();
        let status_transaction = self.transaction.clone();
        RunnableBuilder::new(
            "AuthrsTransport::AboutWebauthn::StatusReceiver",
            move || {
                let _ = interactive_status_callback(status_rx, status_transaction, upcoming_error);
            },
        )
        .may_block(true)
        .dispatch_background_task()?;
        if static_prefs::pref!("security.webauth.webauthn_enable_usbtoken") {
            self.usb_token_manager.lock().unwrap().manage(
                60 * 1000 * 1000,
                status_tx,
                state_callback,
            );
        } else if static_prefs::pref!("security.webauth.webauthn_enable_softtoken") {
            // We don't yet support softtoken
        } else {
            // Silently accept request, if all webauthn-options are disabled.
            // Used for testing.
        }
        Ok(())
    }

    xpcom_method!(run_command => RunCommand(c_cmd: *const nsACString));
    pub fn run_command(&self, c_cmd: &nsACString) -> Result<(), nsresult> {
        // Always test if it can be parsed from incoming JSON (even for tests)
        let incoming: RequestWrapper =
            serde_json::from_str(&c_cmd.to_utf8()).or(Err(NS_ERROR_DOM_OPERATION_ERR))?;
        if static_prefs::pref!("security.webauth.webauthn_enable_usbtoken") {
            let guard = self.transaction.lock().unwrap();
            let puat = guard.as_ref().and_then(|g| g.puat_cache.clone());
            let command = match incoming {
                RequestWrapper::Quit => InteractiveRequest::Quit,
                RequestWrapper::ChangePIN(a, b) => InteractiveRequest::ChangePIN(a, b),
                RequestWrapper::SetPIN(a) => InteractiveRequest::SetPIN(a),
                RequestWrapper::CredentialManagement(c) => {
                    InteractiveRequest::CredentialManagement(c, puat)
                }
                RequestWrapper::BioEnrollment(c) => InteractiveRequest::BioEnrollment(c, puat),
            };
            match &guard.as_ref().unwrap().interactive_receiver {
                Some(channel) => channel.send(command).or(Err(NS_ERROR_FAILURE)),
                // Either we weren't expecting a pin, or the controller is confused
                // about which transaction is active. Neither is recoverable, so it's
                // OK to drop the PinReceiver here.
                _ => Err(NS_ERROR_FAILURE),
            }
        } else if static_prefs::pref!("security.webauth.webauthn_enable_softtoken") {
            // We don't yet support softtoken
            Ok(())
        } else {
            // Silently accept request, if all webauthn-options are disabled.
            // Used for testing.
            Ok(())
        }
    }
}

#[no_mangle]
pub extern "C" fn authrs_service_constructor(result: *mut *const nsIWebAuthnService) -> nsresult {
    let wrapper = AuthrsService::allocate(InitAuthrsService {
        usb_token_manager: Mutex::new(StateMachine::new()),
        test_token_manager: TestTokenManager::new(),
        transaction: Arc::new(Mutex::new(None)),
    });

    #[cfg(feature = "fuzzing")]
    {
        let fuzzing_config = static_prefs::pref!("fuzzing.webauthn.authenticator_config");
        if fuzzing_config != 0 {
            let is_user_verified = (fuzzing_config & 0x01) != 0;
            let is_user_consenting = (fuzzing_config & 0x02) != 0;
            let has_user_verification = (fuzzing_config & 0x04) != 0;
            let has_resident_key = (fuzzing_config & 0x08) != 0;
            let transport = nsCString::from(match (fuzzing_config & 0x10) >> 4 {
                0 => "usb",
                1 => "internal",
                _ => unreachable!(),
            });
            let protocol = nsCString::from(match (fuzzing_config & 0x60) >> 5 {
                0 => "", // reserved
                1 => "ctap1/u2f",
                2 => "ctap2",
                3 => "ctap2_1",
                _ => unreachable!(),
            });
            // If this fails it's probably because the protocol bits were zero,
            // we'll just ignore it.
            let _ = wrapper.add_virtual_authenticator(
                &protocol,
                &transport,
                has_resident_key,
                has_user_verification,
                is_user_consenting,
                is_user_verified,
            );
        }
    }

    unsafe {
        RefPtr::new(wrapper.coerce::<nsIWebAuthnService>()).forget(&mut *result);
    }
    NS_OK
}

#[no_mangle]
pub extern "C" fn authrs_webauthn_att_obj_constructor(
    att_obj_bytes: &ThinVec<u8>,
    anonymize: bool,
    result: *mut *const nsIWebAuthnAttObj,
) -> nsresult {
    if result.is_null() {
        return NS_ERROR_NULL_POINTER;
    }

    let mut att_obj: AttestationObject = match serde_cbor::from_slice(att_obj_bytes) {
        Ok(att_obj) => att_obj,
        Err(_) => return NS_ERROR_INVALID_ARG,
    };

    if anonymize {
        att_obj.anonymize();
    }

    let wrapper = WebAuthnAttObj::allocate(InitWebAuthnAttObj { att_obj });

    unsafe {
        RefPtr::new(wrapper.coerce::<nsIWebAuthnAttObj>()).forget(&mut *result);
    }

    NS_OK
}
