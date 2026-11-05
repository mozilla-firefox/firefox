/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

pub mod attestation;
pub mod client_data;
#[allow(dead_code)] // TODO(MS): Remove me asap
pub mod commands;
pub mod preflight;
pub mod server;
pub(crate) mod utils;

use crate::authenticatorservice::{RegisterArgs, SignArgs};
use crate::crypto::COSEAlgorithm;
use crate::ctap2::client_data::ClientDataHash;
use crate::ctap2::commands::authenticator_config::{
    AuthConfigCommand, AuthConfigResult, AuthenticatorConfig,
};
use crate::ctap2::commands::bio_enrollment::{
    BioEnrollment, BioEnrollmentCommand, BioEnrollmentResult, FingerprintSensorInfo,
};
use crate::ctap2::commands::client_pin::{
    ChangeExistingPin, Pin, PinError, PinUvAuthTokenPermission, SetNewPin,
};
use crate::ctap2::commands::credential_management::{
    CredManagementCommand, CredentialList, CredentialListEntry, CredentialManagement,
    CredentialManagementResult, CredentialRpListEntry,
};
use crate::ctap2::commands::get_assertion::{GetAssertion, GetAssertionOptions};
use crate::ctap2::commands::make_credentials::{
    dummy_make_credentials_cmd, MakeCredentials, MakeCredentialsOptions,
};
use crate::ctap2::commands::reset::Reset;
use crate::ctap2::commands::{
    large_blobs, repackage_pin_errors, CommandError, PinUvAuthCommand, PinUvAuthResult,
    RequestCtap2, StatusCode,
};
use crate::ctap2::preflight::{
    do_credential_list_filtering_ctap1, do_credential_list_filtering_ctap2,
    silently_discover_credentials,
};
use crate::ctap2::server::{
    CredentialProtectionPolicy, RelyingParty, ResidentKeyRequirement, UserVerificationRequirement,
};
use crate::errors::{AuthenticatorError, UnsupportedOption};
use crate::statecallback::StateCallback;
use crate::status_update::{send_status, BioEnrollmentCmd, CredManagementCmd, InteractiveUpdate};
use crate::transport::device_selector::{Device, DeviceSelectorEvent};
use crate::transport::{errors::HIDError, hid::HIDDevice, FidoDevice, FidoDeviceIO, FidoProtocol};
use crate::{ManageResult, ResetResult, StatusPinUv, StatusUpdate};
use std::sync::mpsc::{channel, RecvError, Sender};
use std::thread;
use std::time::Duration;

use self::commands::get_info::AuthenticatorVersion;
use self::commands::large_blobs::LargeBlobArrayElement;

macro_rules! unwrap_option {
    ($item: expr, $callback: expr) => {
        match $item {
            Some(r) => r,
            None => {
                $callback.call(Err(AuthenticatorError::Platform));
                return false;
            }
        }
    };
}

macro_rules! unwrap_result {
    ($item: expr, $callback: expr) => {
        match $item {
            Ok(r) => r,
            Err(e) => {
                $callback.call(Err(e.into()));
                return false;
            }
        }
    };
}

macro_rules! handle_errors {
    ($error: expr, $status: expr, $callback: expr, $pin_uv_auth_result: expr, $skip_uv: expr) => {
        let mut _dummy_skip_puap = false;
        let mut _dummy_cached_puat = false;
        handle_errors!(
            $error,
            $status,
            $callback,
            $pin_uv_auth_result,
            $skip_uv,
            _dummy_skip_puap,
            _dummy_cached_puat
        )
    };
    ($error: expr, $status: expr, $callback: expr, $pin_uv_auth_result: expr, $skip_uv: expr, $skip_puap: expr) => {
        let mut _dummy_cached_puat = false;
        handle_errors!(
            $error,
            $status,
            $callback,
            $pin_uv_auth_result,
            $skip_uv,
            $skip_puap,
            _dummy_cached_puat
        )
    };
    ($error: expr, $status: expr, $callback: expr, $pin_uv_auth_result: expr, $skip_uv: expr, $skip_puap: expr, $cached_puat: expr) => {
        match $error {
            HIDError::Command(CommandError::StatusCode(StatusCode::ChannelBusy, _)) => {
                // Channel busy. Client SHOULD retry the request after a short delay.
                thread::sleep(Duration::from_millis(100));
                continue;
            }
            HIDError::Command(CommandError::StatusCode(StatusCode::OperationDenied, _))
            | HIDError::Command(CommandError::StatusCode(StatusCode::PinAuthInvalid, _))
                if matches!($pin_uv_auth_result, PinUvAuthResult::UsingInternalUv) =>
            {
                // This should only happen for CTAP2.0 tokens that use internal UV and failed
                // (e.g. wrong fingerprint used), while doing GetAssertion or MakeCredentials.
                send_status(
                    &$status,
                    StatusUpdate::PinUvError(StatusPinUv::InvalidUv(None)),
                );
                $skip_puap = false;
                continue;
            }
            HIDError::Command(CommandError::StatusCode(StatusCode::PinRequired, _))
                if matches!($pin_uv_auth_result, PinUvAuthResult::UsingInternalUv) =>
            {
                // This should only happen for CTAP2.0 tokens that use internal UV and failed
                // repeatedly, so that we have to fall back to PINs
                $skip_uv = true;
                $skip_puap = false;
                continue;
            }
            HIDError::Command(CommandError::StatusCode(StatusCode::UvBlocked, _))
                if matches!(
                    $pin_uv_auth_result,
                    PinUvAuthResult::SuccessGetPinUvAuthTokenUsingUvWithPermissions(..)
                ) =>
            {
                // This should only happen for CTAP2.1 tokens that use internal UV and failed
                // repeatedly, so that we have to fall back to PINs
                $skip_uv = true;
                $skip_puap = false;
                continue;
            }
            HIDError::Command(CommandError::StatusCode(StatusCode::CredentialExcluded, _)) => {
                $callback.call(Err(AuthenticatorError::CredentialExcluded));
                break;
            }
            HIDError::Command(CommandError::StatusCode(StatusCode::PinAuthInvalid, _))
                if $cached_puat =>
            {
                // We used the cached PUAT, but it was invalid. So we just try again
                // without the cached one and potentially trigger a new PIN/UV entry from
                // the user. This happens e.g. if the PUAT expires, or we get an all-zeros
                // PUAT from outside for whatever reason, etc.
                $cached_puat = false;
                $skip_puap = false;
                continue;
            }
            e => {
                warn!("error happened: {e}");
                $callback.call(Err(AuthenticatorError::HIDError(e)));
                break;
            }
        }
    };
}

fn ask_user_for_pin(
    was_invalid: bool,
    retries: Option<u8>,
    status: &Sender<StatusUpdate>,
) -> Result<Pin, AuthenticatorError> {
    info!("PIN Error that requires user interaction detected. Sending it back and waiting for a reply");
    let (tx, rx) = channel();
    if was_invalid {
        send_status(
            status,
            crate::StatusUpdate::PinUvError(StatusPinUv::InvalidPin(tx, retries)),
        );
    } else {
        send_status(
            status,
            crate::StatusUpdate::PinUvError(StatusPinUv::PinRequired(tx)),
        );
    }
    match rx.recv() {
        Ok(pin) => Ok(pin),
        Err(RecvError) => {
            // recv() can only fail, if the other side is dropping the Sender.
            info!("Callback dropped the channel. Aborting.");
            Err(AuthenticatorError::CancelledByUser)
        }
    }
}

fn ask_user_for_large_blob(
    status: &Sender<StatusUpdate>,
    key: &[u8],
) -> Result<LargeBlobArrayElement, AuthenticatorError> {
    info!(
        "Make credentials was successful. Now asking the user for the additional large blob data"
    );
    let (tx, rx) = channel();
    send_status(status, crate::StatusUpdate::LargeBlobData(tx, key.to_vec()));
    match rx.recv() {
        Ok(elem) => Ok(elem),
        Err(RecvError) => {
            // recv() can only fail, if the other side is dropping the Sender.
            info!("Callback dropped the channel. Aborting.");
            Err(AuthenticatorError::CancelledByUser)
        }
    }
}

/// Try to fetch PinUvAuthToken from the device and derive from it PinUvAuthParam.
/// Prefer UV, fallback to PIN.
/// Prefer newer pinUvAuth-methods, if supported by the device.
fn get_pin_uv_auth_param<Dev: FidoDevice, T: PinUvAuthCommand + RequestCtap2>(
    cmd: &mut T,
    dev: &mut Dev,
    permission: PinUvAuthTokenPermission,
    skip_uv: bool,
    uv_req: UserVerificationRequirement,
    alive: &dyn Fn() -> bool,
    pin: &Option<Pin>,
) -> Result<PinUvAuthResult, AuthenticatorError> {
    // CTAP 2.1 is very specific that the request should either include pinUvAuthParam
    // OR uv=true, but not both at the same time. We now have to decide which (if either)
    // to send. We may omit both values. Will never send an explicit uv=false, because
    //  a) this is the default, and
    //  b) some CTAP 2.0 authenticators return UnsupportedOption when uv=false.

    // We ensure both pinUvAuthParam and uv are not set to start.
    cmd.set_pin_uv_auth_param(None)?;
    cmd.set_uv_option(None);

    // Skip user verification if we're using CTAP1 or if the device does not support CTAP2.
    let info = match (dev.get_protocol(), dev.get_authenticator_info()) {
        (FidoProtocol::CTAP2, Some(info)) => info,
        _ => return Ok(PinUvAuthResult::DeviceIsCtap1),
    };

    // Only use UV, if the device supports it and we don't skip it
    // which happens as a fallback, if UV-usage failed too many times
    // Note: In theory, we could also repeatedly query GetInfo here and check
    //       if uv is set to Some(true), as tokens should set it to Some(false)
    //       if UV is blocked (too many failed attempts). But the CTAP2.0-spec is
    //       vague and I don't trust all tokens to implement it that way. So we
    //       keep track of it ourselves, using `skip_uv`.
    let supports_uv = info.options.user_verification == Some(true);
    let supports_pin = info.options.client_pin.is_some();
    let pin_configured = info.options.client_pin == Some(true);

    // Check if the combination of device-protection and request-options
    // are allowing for 'discouraged', meaning no auth required.
    if cmd.can_skip_user_verification(info, uv_req) {
        return Ok(PinUvAuthResult::NoAuthRequired);
    }

    // Device does not support any (remaining) auth-method
    if (skip_uv || !supports_uv) && !supports_pin {
        if supports_uv && uv_req == UserVerificationRequirement::Required {
            // We should always set the uv option in the Required case, but the CTAP 2.1 spec
            // says 'Platforms MUST NOT include the "uv" option key if the authenticator does
            // not support built-in user verification.' This is to work around some CTAP 2.0
            // authenticators which incorrectly error out with CTAP2_ERR_UNSUPPORTED_OPTION
            // when the "uv" option is set. The RP that requested UV will (hopefully) reject our
            // response in the !supports_uv case.
            cmd.set_uv_option(Some(true));
        }
        return Ok(PinUvAuthResult::NoAuthTypeSupported);
    }

    // Device supports PINs, but a PIN is not configured. Signal that we
    // can complete the operation if the user sets a PIN first.
    if (skip_uv || !supports_uv) && !pin_configured {
        return Err(AuthenticatorError::PinError(PinError::PinNotSet));
    }

    if info.options.pin_uv_auth_token == Some(true) {
        if !skip_uv && supports_uv {
            // CTAP 2.1 - UV
            let pin_auth_token = dev
                .get_pin_uv_auth_token_using_uv_with_permissions(permission, cmd.get_rp_id(), alive)
                .map_err(|e| repackage_pin_errors(dev, e))?;
            cmd.set_pin_uv_auth_param(Some(pin_auth_token.clone()))?;
            Ok(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingUvWithPermissions(pin_auth_token))
        } else {
            // CTAP 2.1 - PIN
            // We did not take the `!skip_uv && supports_uv` branch, so we have
            // `(skip_uv || !supports_uv)`. Moreover we did not exit early in the
            // `(skip_uv || !supports_uv) && !pin_configured` case. So we have
            // `pin_configured`.
            let pin_auth_token = dev
                .get_pin_uv_auth_token_using_pin_with_permissions(
                    pin,
                    permission,
                    cmd.get_rp_id(),
                    alive,
                )
                .map_err(|e| repackage_pin_errors(dev, e))?;
            cmd.set_pin_uv_auth_param(Some(pin_auth_token.clone()))?;
            Ok(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingPinWithPermissions(pin_auth_token))
        }
    } else {
        // CTAP 2.0 fallback
        if !skip_uv && supports_uv && pin.is_none() {
            // If the device supports internal user-verification (e.g. fingerprints),
            // skip PIN-stuff

            // We need the shared secret for HMAC-extension, if it was requested, so we
            // have to establish one
            if cmd.hmac_requested() && info.supports_hmac_secret() {
                let _shared_secret = dev.establish_shared_secret(alive)?;
            }
            // CTAP 2.1, Section 6.1.1, Step 1.1.2.1.2.
            cmd.set_uv_option(Some(true));
            return Ok(PinUvAuthResult::UsingInternalUv);
        }

        let pin_auth_token = dev
            .get_pin_token(pin, alive)
            .map_err(|e| repackage_pin_errors(dev, e))?;
        cmd.set_pin_uv_auth_param(Some(pin_auth_token.clone()))?;
        Ok(PinUvAuthResult::SuccessGetPinToken(pin_auth_token))
    }
}

/// PUAP, as per spec: PinUvAuthParam
/// Determines, if we need to establish a PinUvAuthParam, based on the
/// capabilities of the device and the incoming request.
/// If it is needed, tries to establish one and save it inside the Request.
/// Returns Ok() if we can proceed with sending the actual Request to
/// the device, Err() otherwise.
/// Handles asking the user for a PIN, if needed and sending StatusUpdates
/// regarding PIN and UV usage.
#[allow(clippy::too_many_arguments)]
fn determine_puap_if_needed<Dev: FidoDevice, T: PinUvAuthCommand + RequestCtap2>(
    cmd: &mut T,
    dev: &mut Dev,
    mut skip_uv: bool,
    permission: PinUvAuthTokenPermission,
    uv_req: UserVerificationRequirement,
    status: &Sender<StatusUpdate>,
    alive: &dyn Fn() -> bool,
    pin: &mut Option<Pin>,
) -> Result<PinUvAuthResult, AuthenticatorError> {
    while alive() {
        debug!("-----------------------------------------------------------------");
        debug!("Getting pinUvAuthParam");
        match get_pin_uv_auth_param(cmd, dev, permission, skip_uv, uv_req, alive, pin) {
            Ok(r) => {
                return Ok(r);
            }

            Err(AuthenticatorError::PinError(PinError::PinRequired)) => {
                let new_pin = ask_user_for_pin(false, None, status)?;
                *pin = Some(new_pin);
                skip_uv = true;
                continue;
            }
            Err(AuthenticatorError::PinError(PinError::InvalidPin(retries))) => {
                let new_pin = ask_user_for_pin(true, retries, status)?;
                *pin = Some(new_pin);
                continue;
            }
            Err(AuthenticatorError::PinError(PinError::InvalidUv(retries))) => {
                if retries == Some(0) {
                    skip_uv = true;
                }
                send_status(
                    status,
                    StatusUpdate::PinUvError(StatusPinUv::InvalidUv(retries)),
                )
            }
            Err(e @ AuthenticatorError::PinError(PinError::PinAuthBlocked)) => {
                send_status(
                    status,
                    StatusUpdate::PinUvError(StatusPinUv::PinAuthBlocked),
                );
                error!("Error when determining pinAuth: {:?}", e);
                return Err(e);
            }
            Err(e @ AuthenticatorError::PinError(PinError::PinBlocked)) => {
                send_status(status, StatusUpdate::PinUvError(StatusPinUv::PinBlocked));
                error!("Error when determining pinAuth: {:?}", e);
                return Err(e);
            }
            Err(e @ AuthenticatorError::PinError(PinError::PinNotSet)) => {
                send_status(status, StatusUpdate::PinUvError(StatusPinUv::PinNotSet));
                error!("Error when determining pinAuth: {:?}", e);
                return Err(e);
            }
            Err(AuthenticatorError::PinError(PinError::UvBlocked)) => {
                skip_uv = true;
                send_status(status, StatusUpdate::PinUvError(StatusPinUv::UvBlocked))
            }
            // Used for CTAP2.0 UV (fingerprints)
            Err(AuthenticatorError::PinError(PinError::PinAuthInvalid)) => {
                skip_uv = true;
                send_status(
                    status,
                    StatusUpdate::PinUvError(StatusPinUv::InvalidUv(None)),
                )
            }
            Err(e) => {
                error!("Error when determining pinAuth: {:?}", e);
                return Err(e);
            }
        }
    }
    Err(AuthenticatorError::CancelledByUser)
}

/// Check that the registration request can be processed by a CTAP1 device.
///
/// See [CTAP 2.1 §10.2][0].
///
/// [0]: https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-errata-20220621.html#u2f-authenticatorMakeCredential-interoperability
pub(crate) fn check_ctap1_register_compatibility(args: &RegisterArgs) -> crate::Result<()> {
    if args.resident_key_req == ResidentKeyRequirement::Required {
        return Err(AuthenticatorError::UnsupportedOption(
            UnsupportedOption::ResidentKey,
        ));
    }
    if args.user_verification_req == UserVerificationRequirement::Required {
        return Err(AuthenticatorError::UnsupportedOption(
            UnsupportedOption::UserVerification,
        ));
    }
    if !args
        .pub_cred_params
        .iter()
        .any(|x| x.alg == COSEAlgorithm::ES256)
    {
        return Err(AuthenticatorError::UnsupportedOption(
            UnsupportedOption::PubCredParams,
        ));
    }

    Ok(())
}

pub fn register<Dev: FidoDevice>(
    dev: &mut Dev,
    args: RegisterArgs,
    status: Sender<crate::StatusUpdate>,
    callback: StateCallback<crate::Result<crate::RegisterResult>>,
    alive: &dyn Fn() -> bool,
) -> bool {
    let mut options = MakeCredentialsOptions::default();

    match dev.get_protocol() {
        FidoProtocol::CTAP2 => {
            let info = match dev.get_authenticator_info() {
                Some(info) => info,
                None => {
                    callback.call(Err(HIDError::DeviceNotInitialized.into()));
                    return false;
                }
            };

            // Set options based on the arguments and the device info.
            // The user verification option will be set in `determine_puap_if_needed`.
            options.resident_key = match args.resident_key_req {
                ResidentKeyRequirement::Required => Some(true),
                ResidentKeyRequirement::Preferred => {
                    // Use a resident key if the authenticator supports it
                    Some(info.options.resident_key)
                }
                ResidentKeyRequirement::Discouraged => Some(false),
            };
        }

        FidoProtocol::CTAP1 => {
            if let Err(e) = check_ctap1_register_compatibility(&args) {
                callback.call(Err(e));
                return false;
            }
        }
    }

    // Client extension processing for credProtect:
    // "When enforceCredentialProtectionPolicy is true, and credentialProtectionPolicy's value is
    // [not "Optional"], the platform SHOULD NOT create the credential in a way that does not
    // implement the requested protection policy. (For example, by creating it on an authenticator
    // that does not support this extension.)"
    let dev_supports_cred_protect = dev
        .get_authenticator_info()
        .is_some_and(|info| info.supports_cred_protect());
    if args.extensions.enforce_credential_protection_policy == Some(true)
        && args.extensions.credential_protection_policy
            != Some(CredentialProtectionPolicy::UserVerificationOptional)
        && !dev_supports_cred_protect
    {
        callback.call(Err(AuthenticatorError::UnsupportedOption(
            UnsupportedOption::CredProtect,
        )));
        return false;
    }
    let has_large_blob = args.extensions.large_blob_key == Some(true);

    let mut makecred = MakeCredentials::new(
        ClientDataHash(args.client_data_hash),
        args.relying_party,
        Some(args.user),
        args.pub_cred_params,
        args.exclude_list,
        options,
        args.extensions.into(),
    );

    let mut skip_uv = false;
    let mut pin = args.pin;
    while alive() {
        // Requesting both because pre-flighting (credential list filtering)
        // can potentially send GetAssertion-commands
        let mut permissions =
            PinUvAuthTokenPermission::MakeCredential | PinUvAuthTokenPermission::GetAssertion;
        if has_large_blob {
            permissions |= PinUvAuthTokenPermission::LargeBlobWrite;
        }

        let pin_uv_auth_result = unwrap_result!(
            determine_puap_if_needed(
                &mut makecred,
                dev,
                skip_uv,
                permissions,
                args.user_verification_req,
                &status,
                alive,
                &mut pin,
            ),
            callback
        );
        // Do "pre-flight": Filter the exclude-list
        if dev.get_protocol() == FidoProtocol::CTAP2 {
            makecred.exclude_list = unwrap_result!(
                do_credential_list_filtering_ctap2(
                    dev,
                    &makecred.exclude_list,
                    &makecred.rp,
                    pin_uv_auth_result.get_pin_uv_auth_token(),
                    &status,
                ),
                callback
            );
        } else {
            let key_handle = do_credential_list_filtering_ctap1(
                dev,
                &makecred.exclude_list,
                &makecred.rp,
                &makecred.client_data_hash,
                &status,
            );
            // That handle was already registered with the token
            if key_handle.is_some() {
                // Now we need to send a dummy registration request, to make the token blink
                // Spec says "dummy appid and invalid challenge". We use the same, as we do for
                // making the token blink upon device selection.
                send_status(&status, crate::StatusUpdate::PresenceRequired);
                let msg = dummy_make_credentials_cmd();
                let _ = dev.send_msg_cancellable(&msg, alive, Some(&status)); // Ignore answer, return "CredentialExcluded"
                callback.call(Err(AuthenticatorError::CredentialExcluded));
                return false;
            }
        }

        debug!("------------------------------------------------------------------");
        debug!("{makecred:?} using {pin_uv_auth_result:?}");
        debug!("------------------------------------------------------------------");
        send_status(&status, crate::StatusUpdate::PresenceRequired);
        let resp = dev.send_msg_cancellable(&makecred, alive, Some(&status));
        match resp {
            Ok(result) => {
                if has_large_blob && result.large_blob_key.is_some() {
                    // NOTE: We 'ask' the user for the large blob here, simply to avoid more dependencies. We could do this non-interactively, by adding the plaintext to the RegisterArgs and calculate all necessary things right here, but we would need AEAD-algos and DEFLATE. Leaving that (currently) for the user to do. If we decide to put it here, see ctap2_discoverable_creds.rs for the required steps.
                    if let Ok(blob) =
                        ask_user_for_large_blob(&status, result.large_blob_key.as_ref().unwrap())
                    {
                        if let Err(e) = large_blobs::add_large_blob(
                            dev,
                            alive,
                            blob,
                            pin_uv_auth_result.get_pin_uv_auth_token(),
                        ) {
                            // Only warn but continue, as the actual registering worked.
                            warn!("Failed to write large blob: {e:?}");
                        }
                    }
                }
                callback.call(Ok(result));
                return true;
            }
            Err(e) => {
                handle_errors!(e, status, callback, pin_uv_auth_result, skip_uv);
            }
        }
    }
    false
}

/// Check that the signing request can be processed by a CTAP1 device.
///
/// See [CTAP 2.1 §10.3][0].
///
/// [0]: https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-errata-20220621.html#u2f-authenticatorGetAssertion-interoperability
pub(crate) fn check_ctap1_sign_compatibility(args: &SignArgs) -> crate::Result<()> {
    if args.user_verification_req == UserVerificationRequirement::Required {
        return Err(AuthenticatorError::UnsupportedOption(
            UnsupportedOption::UserVerification,
        ));
    }
    if args.allow_list.is_empty() {
        return Err(AuthenticatorError::UnsupportedOption(
            UnsupportedOption::EmptyAllowList,
        ));
    }

    Ok(())
}

pub fn sign<Dev: FidoDevice>(
    dev: &mut Dev,
    args: SignArgs,
    status: Sender<crate::StatusUpdate>,
    callback: StateCallback<crate::Result<crate::SignResult>>,
    alive: &dyn Fn() -> bool,
) -> bool {
    if dev.get_protocol() == FidoProtocol::CTAP1 {
        if let Err(e) = check_ctap1_sign_compatibility(&args) {
            callback.call(Err(e));
            return false;
        }
    }

    let mut allow_list = args.allow_list;
    let mut rp_id = RelyingParty::from(args.relying_party_id);
    let client_data_hash = ClientDataHash(args.client_data_hash);
    if let Some(ref app_id) = args.extensions.app_id {
        if !allow_list.is_empty() {
            // Try to silently discover U2F credentials that require the FIDO App ID extension. If
            // any are found, we should use the alternate RP ID instead of the provided RP ID.
            let alt_rp_id = RelyingParty::from(app_id);
            let silent_creds = silently_discover_credentials(
                dev,
                &allow_list,
                &alt_rp_id,
                &client_data_hash,
                &status,
            );
            if !silent_creds.is_empty() {
                allow_list = silent_creds;
                rp_id = alt_rp_id;
            }
        }
    }

    let has_large_blob = args.extensions.large_blob_key == Some(true);
    let mut get_assertion = GetAssertion::new(
        client_data_hash,
        rp_id,
        allow_list,
        GetAssertionOptions {
            user_presence: Some(args.user_presence_req),
            user_verification: None,
        },
        args.extensions.into(),
    );

    let mut skip_uv = false;
    let mut pin = args.pin;
    while alive() {
        let pin_uv_auth_result = unwrap_result!(
            determine_puap_if_needed(
                &mut get_assertion,
                dev,
                skip_uv,
                PinUvAuthTokenPermission::GetAssertion,
                args.user_verification_req,
                &status,
                alive,
                &mut pin,
            ),
            callback
        );

        // Do "pre-flight": Filter the allow-list
        let original_allow_list_was_empty = get_assertion.allow_list.is_empty();
        if dev.get_protocol() == FidoProtocol::CTAP2 {
            get_assertion.allow_list = unwrap_result!(
                do_credential_list_filtering_ctap2(
                    dev,
                    &get_assertion.allow_list,
                    &get_assertion.rp,
                    pin_uv_auth_result.get_pin_uv_auth_token(),
                    &status,
                ),
                callback
            );
        } else {
            let key_handle = do_credential_list_filtering_ctap1(
                dev,
                &get_assertion.allow_list,
                &get_assertion.rp,
                &get_assertion.client_data_hash,
                &status,
            );
            match key_handle {
                Some(key_handle) => {
                    get_assertion.allow_list = vec![key_handle];
                }
                None => {
                    get_assertion.allow_list.clear();
                }
            }
        }

        // If the incoming list was not empty, but the filtered list is, we have to error out
        if !original_allow_list_was_empty && get_assertion.allow_list.is_empty() {
            // We have to collect a user interaction
            send_status(&status, crate::StatusUpdate::PresenceRequired);
            let msg = dummy_make_credentials_cmd();
            let _ = dev.send_msg_cancellable(&msg, alive, Some(&status)); // Ignore answer, return "NoCredentials"
            callback.call(Err(HIDError::Command(CommandError::StatusCode(
                StatusCode::NoCredentials,
                None,
            ))
            .into()));
            return false;
        }

        // Use the shared secret in the extensions, if requested
        get_assertion = match get_assertion.process_hmac_secret_and_prf_extension(
            dev.get_shared_secret().map(|s| (s, &pin_uv_auth_result)),
        ) {
            Ok(value) => value,
            Err(e) => {
                callback.call(Err(e));
                return false;
            }
        };

        debug!("------------------------------------------------------------------");
        debug!("{get_assertion:?} using {pin_uv_auth_result:?}");
        debug!("------------------------------------------------------------------");
        send_status(&status, crate::StatusUpdate::PresenceRequired);
        let mut results = match dev.send_msg_cancellable(&get_assertion, alive, Some(&status)) {
            Ok(results) => results,
            Err(e) => {
                handle_errors!(e, status, callback, pin_uv_auth_result, skip_uv);
            }
        };
        // NOTE: We send here the whole array, in order to keep dependencies down. We could
        // extract, decipher and inflate the individual elements here, using the large_blob_key
        // and only return valid elements. But for that, we would need AEAD and DEFLATE-algos.
        let large_blob_array =
            if has_large_blob && results.iter().any(|f| f.large_blob_key.is_some()) {
                match large_blobs::read_large_blob_array(dev, alive) {
                    Ok(x) => Some(x.large_blob_array),
                    Err(e) => {
                        warn!("Failed to read large blob array: {e:?}");
                        None
                    }
                }
            } else {
                None
            };
        if results.len() == 1 {
            let mut result = results.swap_remove(0);
            result.large_blob_array = large_blob_array;
            callback.call(Ok(result));
            return true;
        }
        let (tx, rx) = channel();
        let user_entities = results
            .iter()
            .filter_map(|x| x.assertion.user.clone())
            .collect();
        send_status(
            &status,
            crate::StatusUpdate::SelectResultNotice(tx, user_entities),
        );
        match rx.recv() {
            Ok(Some(index)) if index < results.len() => {
                let mut result = results.swap_remove(index);
                result.large_blob_array = large_blob_array;
                callback.call(Ok(result));
                return true;
            }
            _ => {
                callback.call(Err(AuthenticatorError::CancelledByUser));
                return true;
            }
        }
    }
    false
}

pub(crate) fn reset_helper<T: From<ResetResult>>(
    dev: &mut Device,
    selector: Sender<DeviceSelectorEvent>,
    status: Sender<crate::StatusUpdate>,
    callback: StateCallback<crate::Result<T>>,
    keep_alive: &dyn Fn() -> bool,
) {
    let reset = Reset {};
    info!("Device {:?} continues with the reset process", dev.id());

    debug!("------------------------------------------------------------------");
    debug!("{:?}", reset);
    debug!("------------------------------------------------------------------");
    send_status(&status, crate::StatusUpdate::PresenceRequired);
    let resp = dev.send_cbor_cancellable(&reset, keep_alive, Some(&status));
    if resp.is_ok() {
        // The DeviceSelector could already be dead, but it might also wait
        // for us to respond, in order to cancel all other tokens in case
        // we skipped the "blinking"-action and went straight for the actual
        // request.
        let _ = selector.send(DeviceSelectorEvent::SelectedToken(dev.id()));
    }

    match resp {
        Ok(()) => callback.call(Ok(T::from(()))),
        Err(HIDError::DeviceNotSupported) | Err(HIDError::UnsupportedCommand) => {}
        Err(HIDError::Command(CommandError::StatusCode(StatusCode::ChannelBusy, _))) => {}
        Err(e) => {
            warn!("error happened: {}", e);
            callback.call(Err(AuthenticatorError::HIDError(e)));
        }
    }
}

pub fn set_or_change_pin_helper<T: From<()>, Dev: FidoDevice>(
    dev: &mut Dev,
    mut current_pin: Option<Pin>,
    new_pin: Pin,
    status: Sender<crate::StatusUpdate>,
    callback: StateCallback<crate::Result<T>>,
    alive: &dyn Fn() -> bool,
) {
    let mut shared_secret = match dev.establish_shared_secret(alive) {
        Ok(s) => s,
        Err(e) => {
            callback.call(Err(AuthenticatorError::HIDError(e)));
            return;
        }
    };

    let authinfo = match dev.get_authenticator_info() {
        Some(i) => i.clone(),
        None => {
            callback.call(Err(HIDError::DeviceNotInitialized.into()));
            return;
        }
    };

    // If the device has a min PIN use that, otherwise default to 4 according to Spec
    if new_pin.as_bytes().len() < authinfo.min_pin_length.unwrap_or(4) as usize {
        callback.call(Err(AuthenticatorError::PinError(PinError::PinIsTooShort)));
        return;
    }

    // As per Spec: "Maximum PIN Length: UTF-8 representation MUST NOT exceed 63 bytes"
    if new_pin.as_bytes().len() >= 64 {
        callback.call(Err(AuthenticatorError::PinError(PinError::PinIsTooLong(
            new_pin.as_bytes().len(),
        ))));
        return;
    }

    // Check if a client-pin is already set, or if a new one should be created
    let res = if Some(true) == authinfo.options.client_pin {
        let mut res;
        let mut was_invalid = false;
        let mut retries = None;
        loop {
            // current_pin will only be Some() in the interactive mode (running `manage()`)
            // In case that PIN is wrong, we want to avoid an endless-loop here with re-trying
            // that wrong PIN all the time. So we `take()` it, and only test it once.
            // If that PIN is wrong, we fall back to the "ask_user_for_pin"-method.
            let curr_pin = match current_pin.take() {
                None => match ask_user_for_pin(was_invalid, retries, &status) {
                    Ok(pin) => pin,
                    Err(e) => {
                        callback.call(Err(e));
                        return;
                    }
                },
                Some(pin) => pin,
            };

            res = ChangeExistingPin::new(&authinfo, &shared_secret, &curr_pin, &new_pin)
                .map_err(HIDError::Command)
                .and_then(|msg| dev.send_cbor_cancellable(&msg, alive, Some(&status)))
                .map_err(|e| repackage_pin_errors(dev, e));

            if let Err(AuthenticatorError::PinError(PinError::InvalidPin(r))) = res {
                was_invalid = true;
                retries = r;
                // We need to re-establish the shared secret for the next round.
                match dev.establish_shared_secret(alive) {
                    Ok(s) => {
                        shared_secret = s;
                    }
                    Err(e) => {
                        callback.call(Err(AuthenticatorError::HIDError(e)));
                        return;
                    }
                };

                continue;
            } else {
                break;
            }
        }
        res
    } else {
        dev.send_cbor_cancellable(
            &SetNewPin::new(&shared_secret, &new_pin),
            alive,
            Some(&status),
        )
        .map_err(AuthenticatorError::HIDError)
    };
    // the callback is expecting `Result<(), AuthenticatorError>`, but `ChangeExistingPin`
    // and `SetNewPin` return the default `ClientPinResponse` on success. Just discard it.
    callback.call(res.map(|_| T::from(())));
}

pub(crate) fn bio_enrollment(
    dev: &mut Device,
    puat_result: Option<PinUvAuthResult>,
    command: BioEnrollmentCmd,
    status: Sender<crate::StatusUpdate>,
    callback: StateCallback<crate::Result<crate::ManageResult>>,
    alive: &dyn Fn() -> bool,
) -> bool {
    let authinfo = match dev.get_authenticator_info() {
        Some(i) => i,
        None => {
            callback.call(Err(HIDError::DeviceNotInitialized.into()));
            return false;
        }
    };

    if authinfo.options.bio_enroll.is_none()
        && authinfo.options.user_verification_mgmt_preview.is_none()
    {
        callback.call(Err(AuthenticatorError::HIDError(
            HIDError::UnsupportedCommand,
        )));
        return false;
    }

    let use_legacy_preview = authinfo.options.bio_enroll.is_none();

    // We are not allowed to request the BE-permission using UV, so we have to skip UV
    let mut skip_uv = authinfo.options.uv_bio_enroll != Some(true);
    // Currently not used, but if we want, we can just set the value here.
    let timeout = None;

    let mut bio_cmd = match &command {
        BioEnrollmentCmd::StartNewEnrollment(_name) => BioEnrollment::new(
            BioEnrollmentCommand::EnrollBegin(timeout),
            use_legacy_preview,
        ),
        BioEnrollmentCmd::DeleteEnrollment(id) => BioEnrollment::new(
            BioEnrollmentCommand::RemoveEnrollment(id.clone()),
            use_legacy_preview,
        ),
        BioEnrollmentCmd::ChangeName(id, name) => BioEnrollment::new(
            BioEnrollmentCommand::SetFriendlyName((id.clone(), name.clone())),
            use_legacy_preview,
        ),
        BioEnrollmentCmd::GetEnrollments => BioEnrollment::new(
            BioEnrollmentCommand::EnumerateEnrollments,
            use_legacy_preview,
        ),
        BioEnrollmentCmd::GetFingerprintSensorInfo => BioEnrollment::new(
            BioEnrollmentCommand::GetFingerprintSensorInfo,
            use_legacy_preview,
        ),
    };

    let mut skip_puap = false;
    let mut cached_puat = false; // If we were provided with a cached puat from the outside
    let mut pin_uv_auth_result = puat_result
        .clone()
        .unwrap_or(PinUvAuthResult::NoAuthRequired);
    // See, if we have a cached PUAT with matching permissions.
    match puat_result {
        Some(PinUvAuthResult::SuccessGetPinToken(t))
        | Some(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingUvWithPermissions(t))
        | Some(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingPinWithPermissions(t))
            if !authinfo.versions.contains(&AuthenticatorVersion::FIDO_2_1) // Only 2.1 has a permission-system
                || use_legacy_preview // Preview doesn't use permissions
                || t.permissions
                    .contains(PinUvAuthTokenPermission::BioEnrollment) =>
        {
            skip_puap = true;
            cached_puat = true;
            unwrap_result!(bio_cmd.set_pin_uv_auth_param(Some(t)), callback);
        }
        _ => {}
    }
    let mut pin = None;
    while alive() {
        if !skip_puap {
            pin_uv_auth_result = unwrap_result!(
                determine_puap_if_needed(
                    &mut bio_cmd,
                    dev,
                    skip_uv,
                    PinUvAuthTokenPermission::BioEnrollment,
                    UserVerificationRequirement::Preferred,
                    &status,
                    alive,
                    &mut pin,
                ),
                callback
            );
        }

        debug!("------------------------------------------------------------------");
        debug!("{bio_cmd:?} using {pin_uv_auth_result:?}");
        debug!("------------------------------------------------------------------");

        let resp = dev.send_cbor_cancellable(&bio_cmd, alive, Some(&status));
        match resp {
            Ok(result) => {
                skip_puap = true;
                match bio_cmd.subcommand {
                    BioEnrollmentCommand::EnrollBegin(..)
                    | BioEnrollmentCommand::EnrollCaptureNextSample(..) => {
                        let template_id =
                            if let BioEnrollmentCommand::EnrollCaptureNextSample((id, ..)) =
                                bio_cmd.subcommand
                            {
                                id
                            } else {
                                unwrap_option!(result.template_id, callback)
                            };
                        let last_enroll_sample_status =
                            unwrap_option!(result.last_enroll_sample_status, callback);
                        let remaining_samples = unwrap_option!(result.remaining_samples, callback);

                        send_status(
                            &status,
                            StatusUpdate::InteractiveManagement(
                                InteractiveUpdate::BioEnrollmentUpdate((
                                    BioEnrollmentResult::SampleStatus(
                                        last_enroll_sample_status,
                                        remaining_samples,
                                    ),
                                    Some(pin_uv_auth_result.clone()),
                                )),
                            ),
                        );

                        if remaining_samples == 0 {
                            if let BioEnrollmentCmd::StartNewEnrollment(Some(ref name)) = command {
                                bio_cmd.subcommand = BioEnrollmentCommand::SetFriendlyName((
                                    template_id.to_vec(),
                                    name.clone(),
                                ));
                                // We have to regenerate PUAP here. PUAT hasn't changed, but the content
                                // of the command has changed, and that is part of the PUAP-calculation
                                unwrap_result!(
                                    bio_cmd.set_pin_uv_auth_param(
                                        pin_uv_auth_result.get_pin_uv_auth_token()
                                    ),
                                    callback
                                );
                                continue;
                            } else {
                                let auth_info =
                                    unwrap_option!(dev.refresh_authenticator_info(), callback);
                                send_status(
                                    &status,
                                    StatusUpdate::InteractiveManagement(
                                        InteractiveUpdate::BioEnrollmentUpdate((
                                            BioEnrollmentResult::AddSuccess(auth_info.clone()),
                                            Some(pin_uv_auth_result),
                                        )),
                                    ),
                                );
                                return true;
                            }
                        } else {
                            bio_cmd.subcommand = BioEnrollmentCommand::EnrollCaptureNextSample((
                                template_id,
                                timeout,
                            ));
                            // We have to regenerate PUAP here. PUAT hasn't changed, but the content
                            // of the command has changed, and that is part of the PUAP-calculation
                            unwrap_result!(
                                bio_cmd.set_pin_uv_auth_param(
                                    pin_uv_auth_result.get_pin_uv_auth_token()
                                ),
                                callback
                            );
                            continue;
                        }
                    }
                    BioEnrollmentCommand::EnumerateEnrollments => {
                        let list = result.template_infos.iter().map(|x| x.into()).collect();
                        send_status(
                            &status,
                            StatusUpdate::InteractiveManagement(
                                InteractiveUpdate::BioEnrollmentUpdate((
                                    BioEnrollmentResult::EnrollmentList(list),
                                    Some(pin_uv_auth_result),
                                )),
                            ),
                        );
                        return true;
                    }
                    BioEnrollmentCommand::SetFriendlyName(_) => {
                        let res = match command {
                            BioEnrollmentCmd::StartNewEnrollment(..) => {
                                let auth_info =
                                    unwrap_option!(dev.refresh_authenticator_info(), callback);
                                BioEnrollmentResult::AddSuccess(auth_info.clone())
                            }
                            _ => BioEnrollmentResult::UpdateSuccess,
                        };
                        send_status(
                            &status,
                            StatusUpdate::InteractiveManagement(
                                InteractiveUpdate::BioEnrollmentUpdate((
                                    res,
                                    Some(pin_uv_auth_result),
                                )),
                            ),
                        );
                        return true;
                    }
                    BioEnrollmentCommand::RemoveEnrollment(_) => {
                        let auth_info = unwrap_option!(dev.refresh_authenticator_info(), callback);
                        send_status(
                            &status,
                            StatusUpdate::InteractiveManagement(
                                InteractiveUpdate::BioEnrollmentUpdate((
                                    BioEnrollmentResult::DeleteSuccess(auth_info.clone()),
                                    Some(pin_uv_auth_result),
                                )),
                            ),
                        );
                        return true;
                    }
                    BioEnrollmentCommand::CancelCurrentEnrollment => {
                        callback.call(Ok(ManageResult::Success));
                        return true;
                    }
                    BioEnrollmentCommand::GetFingerprintSensorInfo => {
                        let fingerprint_kind = unwrap_option!(result.fingerprint_kind, callback);
                        let max_capture_samples_required_for_enroll = unwrap_option!(
                            result.max_capture_samples_required_for_enroll,
                            callback
                        );
                        // FIDO_2_1_PRE-devices do not report this field. So we leave it optional.
                        let max_template_friendly_name = result.max_template_friendly_name;
                        send_status(
                            &status,
                            StatusUpdate::InteractiveManagement(
                                InteractiveUpdate::BioEnrollmentUpdate((
                                    BioEnrollmentResult::FingerprintSensorInfo(
                                        FingerprintSensorInfo {
                                            fingerprint_kind,
                                            max_capture_samples_required_for_enroll,
                                            max_template_friendly_name,
                                        },
                                    ),
                                    Some(pin_uv_auth_result),
                                )),
                            ),
                        );
                        return true;
                    }
                };
            }
            Err(e) => {
                handle_errors!(
                    e,
                    status,
                    callback,
                    pin_uv_auth_result,
                    skip_uv,
                    skip_puap,
                    cached_puat
                );
            }
        }
    }
    false
}

pub fn credential_management<Dev: FidoDevice>(
    dev: &mut Dev,
    puat_result: Option<PinUvAuthResult>,
    command: CredManagementCmd,
    status: Sender<crate::StatusUpdate>,
    callback: StateCallback<crate::Result<crate::ManageResult>>,
    alive: &dyn Fn() -> bool,
) -> bool {
    let mut skip_uv = false;
    let authinfo = match dev.get_authenticator_info() {
        Some(i) => i.clone(),
        None => {
            callback.call(Err(HIDError::DeviceNotInitialized.into()));
            return false;
        }
    };

    if authinfo.options.cred_mgmt != Some(true)
        && authinfo.options.credential_mgmt_preview != Some(true)
    {
        callback.call(Err(AuthenticatorError::HIDError(
            HIDError::UnsupportedCommand,
        )));
        return false;
    }

    let use_legacy_preview = authinfo.options.cred_mgmt != Some(true);

    // FIDO_2_1_PRE-devices do not support UpdateUserInformation.
    if use_legacy_preview
        && !authinfo.versions.contains(&AuthenticatorVersion::FIDO_2_1)
        && matches!(command, CredManagementCmd::UpdateUserInformation(..))
    {
        callback.call(Err(AuthenticatorError::HIDError(
            HIDError::UnsupportedCommand,
        )));
        return false;
    }

    // If puap is provided, we can skip puap-determination (i.e. PIN entry)
    let mut cred_management = match command {
        CredManagementCmd::GetCredentials => {
            CredentialManagement::new(CredManagementCommand::GetCredsMetadata, use_legacy_preview)
        }
        CredManagementCmd::DeleteCredential(cred_id) => CredentialManagement::new(
            CredManagementCommand::DeleteCredential(cred_id),
            use_legacy_preview,
        ),
        CredManagementCmd::UpdateUserInformation(cred_id, user) => CredentialManagement::new(
            CredManagementCommand::UpdateUserInformation((cred_id, user)),
            use_legacy_preview,
        ),
    };
    let mut credential_result = CredentialList::new();
    let mut remaining_rps = 0;
    let mut remaining_cred_ids = 0;
    let mut current_rp = 0;
    let mut skip_puap = false;
    let mut cached_puat = false; // If we were provided with a cached puat from the outside
    let mut pin_uv_auth_result = puat_result
        .clone()
        .unwrap_or(PinUvAuthResult::NoAuthRequired);
    // See, if we have a cached PUAT with matching permissions.
    match puat_result {
        Some(PinUvAuthResult::SuccessGetPinToken(t))
        | Some(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingUvWithPermissions(t))
        | Some(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingPinWithPermissions(t))
            if !authinfo.versions.contains(&AuthenticatorVersion::FIDO_2_1) // Only 2.1 has a permission-system
                || use_legacy_preview // Preview doesn't use permissions
                || t.permissions
                    .contains(PinUvAuthTokenPermission::CredentialManagement) =>
        {
            skip_puap = true;
            cached_puat = true;
            unwrap_result!(cred_management.set_pin_uv_auth_param(Some(t)), callback);
        }
        _ => {}
    }
    let mut pin = None;
    while alive() {
        if !skip_puap {
            pin_uv_auth_result = unwrap_result!(
                determine_puap_if_needed(
                    &mut cred_management,
                    dev,
                    skip_uv,
                    PinUvAuthTokenPermission::CredentialManagement,
                    UserVerificationRequirement::Preferred,
                    &status,
                    alive,
                    &mut pin,
                ),
                callback
            );
        }

        debug!("------------------------------------------------------------------");
        debug!("{cred_management:?} using {pin_uv_auth_result:?}");
        debug!("------------------------------------------------------------------");

        let resp = dev.send_cbor_cancellable(&cred_management, alive, Some(&status));
        match resp {
            Ok(result) => {
                skip_puap = true;
                match cred_management.subcommand {
                    CredManagementCommand::GetCredsMetadata => {
                        let existing_resident_credentials_count =
                            unwrap_option!(result.existing_resident_credentials_count, callback);
                        let max_possible_remaining_resident_credentials_count = unwrap_option!(
                            result.max_possible_remaining_resident_credentials_count,
                            callback
                        );
                        credential_result.existing_resident_credentials_count =
                            existing_resident_credentials_count;
                        credential_result.max_possible_remaining_resident_credentials_count =
                            max_possible_remaining_resident_credentials_count;
                        if existing_resident_credentials_count > 0 {
                            cred_management.subcommand = CredManagementCommand::EnumerateRPsBegin;
                            // We have to regenerate PUAP here. PUAT hasn't changed, but the content
                            // of the command has changed, and that is part of the PUAP-calculation
                            unwrap_result!(
                                cred_management.set_pin_uv_auth_param(
                                    pin_uv_auth_result.get_pin_uv_auth_token()
                                ),
                                callback
                            );
                            continue;
                        } else {
                            // This token doesn't have any resident keys, but its not an error,
                            // so we send an empty list.
                            send_status(
                                &status,
                                StatusUpdate::InteractiveManagement(
                                    InteractiveUpdate::CredentialManagementUpdate((
                                        CredentialManagementResult::CredentialList(
                                            credential_result,
                                        ),
                                        Some(pin_uv_auth_result),
                                    )),
                                ),
                            );
                            return true;
                        }
                    }
                    CredManagementCommand::EnumerateRPsBegin
                    | CredManagementCommand::EnumerateRPsGetNextRP => {
                        if matches!(
                            cred_management.subcommand,
                            CredManagementCommand::EnumerateRPsBegin
                        ) {
                            let total_rps = unwrap_option!(result.total_rps, callback);
                            if total_rps == 0 {
                                // This token doesn't have any RPs, but its not an error,
                                // so we return an Ok with an empty list.
                                send_status(
                                    &status,
                                    StatusUpdate::InteractiveManagement(
                                        InteractiveUpdate::CredentialManagementUpdate((
                                            CredentialManagementResult::CredentialList(
                                                credential_result,
                                            ),
                                            Some(pin_uv_auth_result),
                                        )),
                                    ),
                                );
                                return true;
                            }
                            remaining_rps = total_rps - 1;
                        } else {
                            remaining_rps -= 1;
                        }

                        let rp = unwrap_option!(result.rp, callback);
                        let rp_id_hash = unwrap_option!(result.rp_id_hash, callback);
                        let rp_res = CredentialRpListEntry {
                            rp,
                            rp_id_hash,
                            credentials: vec![],
                        };
                        credential_result.credential_list.push(rp_res);
                        if remaining_rps > 0 {
                            cred_management.subcommand =
                                CredManagementCommand::EnumerateRPsGetNextRP;
                        } else {
                            // We have queried all RPs, now start querying the corresponding credentials for each RP
                            cred_management.subcommand =
                                CredManagementCommand::EnumerateCredentialsBegin(
                                    credential_result.credential_list[0].rp_id_hash.clone(),
                                );
                        }
                        // We have to regenerate PUAP here. PUAT hasn't changed, but the content
                        // of the command has changed, and that is part of the PUAP-calculation
                        unwrap_result!(
                            cred_management
                                .set_pin_uv_auth_param(pin_uv_auth_result.get_pin_uv_auth_token()),
                            callback
                        );
                        continue;
                    }
                    CredManagementCommand::EnumerateCredentialsBegin(..)
                    | CredManagementCommand::EnumerateCredentialsGetNextCredential => {
                        let user = unwrap_option!(result.user, callback);
                        let credential_id = unwrap_option!(result.credential_id, callback);
                        let public_key = unwrap_option!(result.public_key, callback);
                        let cred_protect = unwrap_option!(result.cred_protect, callback);
                        let large_blob_key = result.large_blob_key;

                        if matches!(
                            cred_management.subcommand,
                            CredManagementCommand::EnumerateCredentialsBegin(..)
                        ) {
                            remaining_cred_ids =
                                unwrap_option!(result.total_credentials, callback) - 1;
                        } else {
                            remaining_cred_ids -= 1;
                        }
                        // We might have to change the global variable, but need the unmodified below
                        let current_rp_backup = current_rp;
                        let mut we_are_done = false;
                        if remaining_cred_ids > 0 {
                            cred_management.subcommand =
                                CredManagementCommand::EnumerateCredentialsGetNextCredential;
                            // Clears the PUAP of the previous command; GetNextCredential
                            // is sent without one.
                            unwrap_result!(
                                cred_management.set_pin_uv_auth_param(
                                    pin_uv_auth_result.get_pin_uv_auth_token()
                                ),
                                callback
                            );
                        } else {
                            current_rp += 1;
                            // We have all credentials from this RP. Starting with the next RP.
                            if current_rp < credential_result.credential_list.len() {
                                cred_management.subcommand =
                                    CredManagementCommand::EnumerateCredentialsBegin(
                                        credential_result.credential_list[current_rp]
                                            .rp_id_hash
                                            .clone(),
                                    );
                                // We have to regenerate PUAP here. PUAT hasn't changed, but the content
                                // of the command has changed, and that is part of the PUAP-calculation
                                unwrap_result!(
                                    cred_management.set_pin_uv_auth_param(
                                        pin_uv_auth_result.get_pin_uv_auth_token()
                                    ),
                                    callback
                                );
                            } else {
                                // Finally done iterating over all RPs and their Credentials
                                we_are_done = true;
                            }
                        }
                        let key = CredentialListEntry {
                            user,
                            credential_id,
                            public_key,
                            cred_protect,
                            large_blob_key,
                        };
                        credential_result.credential_list[current_rp_backup]
                            .credentials
                            .push(key);
                        if we_are_done {
                            send_status(
                                &status,
                                StatusUpdate::InteractiveManagement(
                                    InteractiveUpdate::CredentialManagementUpdate((
                                        CredentialManagementResult::CredentialList(
                                            credential_result,
                                        ),
                                        Some(pin_uv_auth_result),
                                    )),
                                ),
                            );
                            return true;
                        } else {
                            continue;
                        }
                    }
                    CredManagementCommand::DeleteCredential(_) => {
                        send_status(
                            &status,
                            StatusUpdate::InteractiveManagement(
                                InteractiveUpdate::CredentialManagementUpdate((
                                    CredentialManagementResult::DeleteSucess,
                                    Some(pin_uv_auth_result),
                                )),
                            ),
                        );
                        return true;
                    }
                    CredManagementCommand::UpdateUserInformation(_) => {
                        send_status(
                            &status,
                            StatusUpdate::InteractiveManagement(
                                InteractiveUpdate::CredentialManagementUpdate((
                                    CredentialManagementResult::UpdateSuccess,
                                    Some(pin_uv_auth_result),
                                )),
                            ),
                        );
                        return true;
                    }
                };
            }
            Err(e) => {
                handle_errors!(
                    e,
                    status,
                    callback,
                    pin_uv_auth_result,
                    skip_uv,
                    skip_puap,
                    cached_puat
                );
            }
        }
    }
    false
}

pub(crate) fn configure_authenticator(
    dev: &mut Device,
    puat_result: Option<PinUvAuthResult>,
    cfg_subcommand: AuthConfigCommand,
    status: Sender<crate::StatusUpdate>,
    callback: StateCallback<crate::Result<crate::ManageResult>>,
    alive: &dyn Fn() -> bool,
) -> bool {
    let mut authcfg = AuthenticatorConfig::new(cfg_subcommand);
    let mut skip_uv = false;
    let authinfo = match dev.get_authenticator_info() {
        Some(i) => i.clone(),
        None => {
            callback.call(Err(HIDError::DeviceNotInitialized.into()));
            return false;
        }
    };

    if authinfo.options.authnr_cfg != Some(true) {
        callback.call(Err(AuthenticatorError::HIDError(
            HIDError::UnsupportedCommand,
        )));
        return false;
    }

    let mut skip_puap = false;
    let mut cached_puat = false; // If we were provided with a cached puat from the outside
    let mut pin_uv_auth_result = puat_result
        .clone()
        .unwrap_or(PinUvAuthResult::NoAuthRequired);
    match puat_result {
        Some(PinUvAuthResult::SuccessGetPinToken(t))
        | Some(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingUvWithPermissions(t))
        | Some(PinUvAuthResult::SuccessGetPinUvAuthTokenUsingPinWithPermissions(t))
            if t.permissions
                .contains(PinUvAuthTokenPermission::AuthenticatorConfiguration) =>
        {
            skip_puap = true;
            cached_puat = true;
            unwrap_result!(authcfg.set_pin_uv_auth_param(Some(t)), callback);
        }
        _ => {}
    }
    let mut pin = None;
    while alive() {
        // We can use the AuthenticatorConfiguration-command only in two cases:
        // 1. The device also supports the uv_acfg-permission (otherwise we can't establish a PUAP)
        // 2. The device is NOT protected by PIN/UV (yet). This allows organizations to configure
        //    the token, before handing them out.
        // If authinfo.options.uv_acfg is not supported, this will return UnauthorizedPermission
        if !skip_puap {
            pin_uv_auth_result = unwrap_result!(
                determine_puap_if_needed(
                    &mut authcfg,
                    dev,
                    skip_uv,
                    PinUvAuthTokenPermission::AuthenticatorConfiguration,
                    UserVerificationRequirement::Preferred,
                    &status,
                    alive,
                    &mut pin,
                ),
                callback
            );
        }

        debug!("------------------------------------------------------------------");
        debug!("{authcfg:?} using {pin_uv_auth_result:?}");
        debug!("------------------------------------------------------------------");

        let resp = dev.send_cbor_cancellable(&authcfg, alive, Some(&status));
        match resp {
            Ok(()) => {
                let auth_info = unwrap_option!(dev.refresh_authenticator_info(), callback);
                send_status(
                    &status,
                    StatusUpdate::InteractiveManagement(InteractiveUpdate::AuthConfigUpdate((
                        AuthConfigResult::Success(auth_info.clone()),
                        Some(pin_uv_auth_result),
                    ))),
                );
                return true;
            }
            Err(e) => {
                handle_errors!(
                    e,
                    status,
                    callback,
                    pin_uv_auth_result,
                    skip_uv,
                    skip_puap,
                    cached_puat
                );
            }
        }
    }
    false
}
