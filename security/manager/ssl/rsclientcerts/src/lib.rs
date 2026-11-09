/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate pkcs11_bindings;

pub mod cryptoki;
pub mod manager;

// Helper macro to prefix log messages with the current thread ID.
#[macro_export]
macro_rules! log_with_thread_id {
    ($log_level:ident, $($message:expr),*) => {
        $log_level!("{:?} {}", std::thread::current().id(), format_args!($($message),*));
    };
}

// This module defines a few helper macros that can be used to declare the repetitive, boilerplate
// code required to implement a PKCS#11 module. They require the macros `try_to_get_manager_guard`
// and `manager_guard_to_manager` to be defined. Generally speaking, these manager macros are used
// to get a mutable handle on a `Mutex<Option<Manager<...>>>` that represents the state of the
// module.

/// NB: Requires MANUFACTURER_ID_BYTES and LIBRARY_DESCRIPTION_BYTES to be defined.
#[macro_export]
macro_rules! declare_pkcs11_informational_functions {
    () => {
        /// This gets called to gather some information about the module. In particular, this
        /// implementation supports (portions of) cryptoki (PKCS #11) version 2.2.
        extern "C" fn C_GetInfo(pInfo: CK_INFO_PTR) -> CK_RV {
            if pInfo.is_null() {
                log_with_thread_id!(error, "C_GetInfo: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let info = CK_INFO {
                cryptokiVersion: CK_VERSION { major: 2, minor: 2 },
                manufacturerID: *MANUFACTURER_ID_BYTES,
                flags: 0,
                libraryDescription: *LIBRARY_DESCRIPTION_BYTES,
                libraryVersion: CK_VERSION { major: 0, minor: 0 },
            };
            unsafe {
                *pInfo = info;
            }
            log_with_thread_id!(debug, "C_GetInfo: CKR_OK");
            CKR_OK
        }

        /// This gets called twice: once with a null `pSlotList` to get the number of slots
        /// (returned via `pulCount`) and a second time to get the ID for each slot.
        extern "C" fn C_GetSlotList(
            tokenPresent: CK_BBOOL,
            pSlotList: CK_SLOT_ID_PTR,
            pulCount: CK_ULONG_PTR,
        ) -> CK_RV {
            if pulCount.is_null() {
                log_with_thread_id!(error, "C_GetSlotList: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let slot_ids = manager.get_slot_ids(if tokenPresent == CK_TRUE { true } else { false });
            let slot_count: CK_ULONG = slot_ids.len().try_into().unwrap();
            if !pSlotList.is_null() {
                if unsafe { *pulCount } < slot_count {
                    log_with_thread_id!(error, "C_GetSlotList: CKR_BUFFER_TOO_SMALL");
                    return CKR_BUFFER_TOO_SMALL;
                }
                unsafe {
                    std::ptr::copy_nonoverlapping(slot_ids.as_ptr(), pSlotList, slot_ids.len());
                }
            };
            unsafe {
                *pulCount = slot_count;
            }
            log_with_thread_id!(debug, "C_GetSlotList: CKR_OK");
            CKR_OK
        }

        extern "C" fn C_GetSlotInfo(slotID: CK_SLOT_ID, pInfo: CK_SLOT_INFO_PTR) -> CK_RV {
            if pInfo.is_null() {
                log_with_thread_id!(error, "C_GetSlotInfo: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let Ok(slot_info) = manager.get_slot_info(slotID) else {
                log_with_thread_id!(error, "C_GetSlotInfo: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            };
            unsafe {
                *pInfo = slot_info;
            }
            log_with_thread_id!(debug, "C_GetSlotInfo: CKR_OK");
            CKR_OK
        }

        extern "C" fn C_GetTokenInfo(slotID: CK_SLOT_ID, pInfo: CK_TOKEN_INFO_PTR) -> CK_RV {
            if pInfo.is_null() {
                log_with_thread_id!(error, "C_GetTokenInfo: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let Ok(token_info) = manager.get_token_info(slotID) else {
                log_with_thread_id!(error, "C_GetTokenInfo: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            };
            unsafe {
                *pInfo = token_info;
            }
            log_with_thread_id!(debug, "C_GetTokenInfo: CKR_OK");
            CKR_OK
        }

        extern "C" fn C_GetMechanismList(
            slotID: CK_SLOT_ID,
            pMechanismList: CK_MECHANISM_TYPE_PTR,
            pulCount: CK_ULONG_PTR,
        ) -> CK_RV {
            if pulCount.is_null() {
                log_with_thread_id!(error, "C_GetMechanismList: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let Ok(mechanisms) = manager.get_mechanism_list(slotID) else {
                log_with_thread_id!(error, "C_GetMechanismList: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            };
            let mechanisms_len: CK_ULONG = mechanisms.len().try_into().unwrap();
            if !pMechanismList.is_null() {
                if unsafe { *pulCount } < mechanisms_len {
                    log_with_thread_id!(error, "C_GetMechanismList: CKR_ARGUMENTS_BAD");
                    return CKR_ARGUMENTS_BAD;
                }
                unsafe {
                    std::ptr::copy_nonoverlapping(
                        mechanisms.as_ptr(),
                        pMechanismList,
                        mechanisms.len(),
                    );
                }
            }
            unsafe {
                *pulCount = mechanisms_len;
            }
            log_with_thread_id!(debug, "C_GetMechanismList: CKR_OK");
            CKR_OK
        }
    };
}

#[macro_export]
macro_rules! declare_pkcs11_session_functions {
    () => {
        extern "C" fn C_OpenSession(
            slotID: CK_SLOT_ID,
            _flags: CK_FLAGS,
            _pApplication: CK_VOID_PTR,
            _Notify: CK_NOTIFY,
            phSession: CK_SESSION_HANDLE_PTR,
        ) -> CK_RV {
            if phSession.is_null() {
                log_with_thread_id!(error, "C_OpenSession: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let session_handle = match manager.open_session(slotID) {
                Ok(session_handle) => session_handle,
                Err(e) => {
                    log_with_thread_id!(error, "C_OpenSession: open_session failed: {}", e);
                    return CKR_DEVICE_ERROR;
                }
            };
            unsafe {
                *phSession = session_handle;
            }
            log_with_thread_id!(debug, "C_OpenSession: CKR_OK");
            CKR_OK
        }

        extern "C" fn C_CloseSession(hSession: CK_SESSION_HANDLE) -> CK_RV {
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            if manager.close_session(hSession).is_err() {
                log_with_thread_id!(error, "C_CloseSession: CKR_SESSION_HANDLE_INVALID");
                return CKR_SESSION_HANDLE_INVALID;
            }
            log_with_thread_id!(debug, "C_CloseSession: CKR_OK");
            CKR_OK
        }

        extern "C" fn C_CloseAllSessions(slotID: CK_SLOT_ID) -> CK_RV {
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            match manager.close_all_sessions(slotID) {
                Ok(()) => {
                    log_with_thread_id!(debug, "C_CloseAllSessions: CKR_OK");
                    CKR_OK
                }
                Err(e) => {
                    log_with_thread_id!(
                        error,
                        "C_CloseAllSessions: close_all_sessions failed: {}",
                        e
                    );
                    CKR_DEVICE_ERROR
                }
            }
        }

        extern "C" fn C_GetSessionInfo(
            hSession: CK_SESSION_HANDLE,
            pInfo: CK_SESSION_INFO_PTR,
        ) -> CK_RV {
            if pInfo.is_null() {
                log_with_thread_id!(error, "C_GetSessionInfo: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let Ok(session_info) = manager.get_session_info(hSession) else {
                log_with_thread_id!(error, "C_GetSessionInfo: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            };
            unsafe {
                *pInfo = session_info;
            }
            log_with_thread_id!(debug, "C_GetSessionInfo: CKR_OK");
            CKR_OK
        }

        extern "C" fn C_Login(
            hSession: CK_SESSION_HANDLE,
            _userType: CK_USER_TYPE,
            pPin: CK_UTF8CHAR_PTR,
            ulPinLen: CK_ULONG,
        ) -> CK_RV {
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let pin = unsafe { rsclientcerts::cryptoki::char_ptr_to_slice(pPin, ulPinLen) };
            match manager.login(hSession, pin) {
                Ok(()) => {
                    log_with_thread_id!(debug, "C_Login: CKR_OK");
                    CKR_OK
                }
                Err(e) => {
                    log_with_thread_id!(error, "C_Login failed: {}", e);
                    CKR_PIN_INCORRECT
                }
            }
        }

        extern "C" fn C_Logout(hSession: CK_SESSION_HANDLE) -> CK_RV {
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            match manager.logout(hSession) {
                Ok(()) => {
                    log_with_thread_id!(debug, "C_Logout: CKR_OK");
                    CKR_OK
                }
                Err(e) => {
                    log_with_thread_id!(error, "C_Logout failed: {}", e);
                    CKR_GENERAL_ERROR
                }
            }
        }
    };
}

#[macro_export]
macro_rules! declare_pkcs11_find_functions {
    () => {
        fn trace_attr(prefix: &str, attr: &CK_ATTRIBUTE, print_value: bool) {
            // Copying out the fields of `attr` avoids making a reference to an unaligned field.
            let typ = attr.type_;
            let typ = match typ {
                CKA_CERTIFICATE_TYPE => "CKA_CERTIFICATE_TYPE".to_string(),
                CKA_CLASS => "CKA_CLASS".to_string(),
                CKA_EC_PARAMS => "CKA_EC_PARAMS".to_string(),
                CKA_ID => "CKA_ID".to_string(),
                CKA_ISSUER => "CKA_ISSUER".to_string(),
                CKA_KEY_TYPE => "CKA_KEY_TYPE".to_string(),
                CKA_HASH_OF_CERTIFICATE => "CKA_HASH_OF_CERTIFICATE".to_string(),
                CKA_NAME_HASH_ALGORITHM => "CKA_NAME_HASH_ALGORITHM".to_string(),
                CKA_LABEL => "CKA_LABEL".to_string(),
                CKA_MODULUS => "CKA_MODULUS".to_string(),
                CKA_PRIVATE => "CKA_PRIVATE".to_string(),
                CKA_SERIAL_NUMBER => "CKA_SERIAL_NUMBER".to_string(),
                CKA_SUBJECT => "CKA_SUBJECT".to_string(),
                CKA_TOKEN => "CKA_TOKEN".to_string(),
                CKA_VALUE => "CKA_VALUE".to_string(),
                nss::CKA_PKCS_TRUST_CLIENT_AUTH => "CKA_TRUST_CLIENT_AUTH".to_string(),
                nss::CKA_PKCS_TRUST_CODE_SIGNING => "CKA_TRUST_CODE_SIGNING".to_string(),
                nss::CKA_PKCS_TRUST_EMAIL_PROTECTION => "CKA_TRUST_EMAIL_PROTECTION".to_string(),
                nss::CKA_PKCS_TRUST_SERVER_AUTH => "CKA_TRUST_SERVER_AUTH".to_string(),
                _ => format!("0x{:x}", typ),
            };
            if print_value {
                let value =
                    unsafe { std::slice::from_raw_parts(attr.pValue as *const u8, attr.ulValueLen as usize) };
                let value_hex = value.into_iter().map(|b| format!("{b:02x}")).collect::<Vec<String>>().join("");
                let len = attr.ulValueLen;
                log_with_thread_id!(
                    trace,
                    "{prefix}CK_ATTRIBUTE {{ type: {typ}, pValue: {value_hex}, ulValueLen: {len} }}"
                );
            } else {
                let len = attr.ulValueLen;
                log_with_thread_id!(trace, "{prefix}CK_ATTRIBUTE {{ type: {typ}, ulValueLen: {len} }}");
            }
        }

        /// This gets called to initialize a search for objects matching a given list of attributes.
        extern "C" fn C_FindObjectsInit(
            hSession: CK_SESSION_HANDLE,
            pTemplate: CK_ATTRIBUTE_PTR,
            ulCount: CK_ULONG,
        ) -> CK_RV {
            if pTemplate.is_null() {
                log_with_thread_id!(error, "C_FindObjectsInit: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut attrs = Vec::new();
            log_with_thread_id!(trace, "C_FindObjectsInit:");
            for i in 0..ulCount as usize {
                let attr = unsafe { &*pTemplate.add(i) };
                trace_attr("  ", attr, true);
                // Copy out the attribute type to avoid making a reference to an unaligned field.
                let attr_type = attr.type_;
                let slice = unsafe {
                    std::slice::from_raw_parts(attr.pValue as *const u8, attr.ulValueLen as usize)
                };
                attrs.push((attr_type, slice.to_owned()));
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            match manager.start_search(hSession, attrs) {
                Ok(()) => {}
                Err(e) => {
                    log_with_thread_id!(error, "C_FindObjectsInit: CKR_ARGUMENTS_BAD: {}", e);
                    return CKR_ARGUMENTS_BAD;
                }
            }
            log_with_thread_id!(debug, "C_FindObjectsInit: CKR_OK");
            CKR_OK
        }

        /// This gets called after `C_FindObjectsInit` to get the results of a search.
        extern "C" fn C_FindObjects(
            hSession: CK_SESSION_HANDLE,
            phObject: CK_OBJECT_HANDLE_PTR,
            ulMaxObjectCount: CK_ULONG,
            pulObjectCount: CK_ULONG_PTR,
        ) -> CK_RV {
            if phObject.is_null() || pulObjectCount.is_null() || ulMaxObjectCount == 0 {
                log_with_thread_id!(error, "C_FindObjects: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let handles = match manager.search(hSession, ulMaxObjectCount as usize) {
                Ok(handles) => handles,
                Err(e) => {
                    log_with_thread_id!(error, "C_FindObjects: CKR_ARGUMENTS_BAD: {}", e);
                    return CKR_ARGUMENTS_BAD;
                }
            };
            log_with_thread_id!(debug, "C_FindObjects: found handles {:?}", handles);
            if handles.len() > ulMaxObjectCount as usize {
                log_with_thread_id!(error, "C_FindObjects: manager returned too many handles");
                return CKR_DEVICE_ERROR;
            }
            unsafe {
                *pulObjectCount = handles.len() as CK_ULONG;
            }
            for (index, handle) in handles.iter().enumerate() {
                if index < ulMaxObjectCount as usize {
                    unsafe {
                        *(phObject.add(index)) = *handle;
                    }
                }
            }
            log_with_thread_id!(debug, "C_FindObjects: CKR_OK");
            CKR_OK
        }

        /// This gets called after `C_FindObjectsInit` and `C_FindObjects` to finish a search.
        extern "C" fn C_FindObjectsFinal(hSession: CK_SESSION_HANDLE) -> CK_RV {
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            // It would be an error if there were no search for this session, but we can be permissive here.
            match manager.clear_search(hSession) {
                Ok(()) => {
                    log_with_thread_id!(debug, "C_FindObjectsFinal: CKR_OK");
                    CKR_OK
                }
                Err(e) => {
                    log_with_thread_id!(error, "C_FindObjectsFinal: clear_search failed: {}", e);
                    CKR_DEVICE_ERROR
                }
            }
        }

        /// This gets called to obtain the values of a number of attributes of an object identified
        /// by the given handle. If a specified attribute is not defined on the object, the length
        /// of that attribute is set to CK_UNAVAILABLE_INFORMATION to indicate that it is not
        /// available. This gets called twice: once to obtain the lengths of the attributes and
        /// again to get the values.
        extern "C" fn C_GetAttributeValue(
            hSession: CK_SESSION_HANDLE,
            hObject: CK_OBJECT_HANDLE,
            pTemplate: CK_ATTRIBUTE_PTR,
            ulCount: CK_ULONG,
        ) -> CK_RV {
            if pTemplate.is_null() {
                log_with_thread_id!(error, "C_GetAttributeValue: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let mut attr_types = Vec::with_capacity(ulCount as usize);
            log_with_thread_id!(trace, "C_GetAttributeValue:");
            for i in 0..ulCount as usize {
                let attr = unsafe { &*pTemplate.add(i) };
                trace_attr("  ", attr, false);
                attr_types.push(attr.type_);
            }
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let values = match manager.get_attributes(hSession, hObject, attr_types) {
                Ok(values) => values,
                Err(e) => {
                    log_with_thread_id!(error, "C_GetAttributeValue: CKR_ARGUMENTS_BAD ({})", e);
                    return CKR_ARGUMENTS_BAD;
                }
            };
            if values.len() != ulCount as usize {
                log_with_thread_id!(
                    error,
                    "C_GetAttributeValue: manager.get_attributes didn't return the right number of values"
                );
                return CKR_DEVICE_ERROR;
            }
            for (i, value) in values.iter().enumerate().take(ulCount as usize) {
                let attr = unsafe { &mut *pTemplate.add(i) };
                if let Some(attr_value) = value {
                    if !attr.pValue.is_null() {
                        let ptr: *mut u8 = attr.pValue as *mut u8;
                        if attr_value.len() > attr.ulValueLen as usize {
                            log_with_thread_id!(error, "C_GetAttributeValue: insufficient attr size");
                            return CKR_ARGUMENTS_BAD;
                        }
                        unsafe {
                            std::ptr::copy_nonoverlapping(attr_value.as_ptr(), ptr, attr_value.len());
                        }
                    }
                    attr.ulValueLen = attr_value.len() as CK_ULONG;
                } else {
                    attr.ulValueLen = CK_UNAVAILABLE_INFORMATION;
                }
            }
            log_with_thread_id!(debug, "C_GetAttributeValue: CKR_OK");
            CKR_OK
        }

    };
}

#[macro_export]
macro_rules! declare_pkcs11_sign_functions {
    () => {
        /// This gets called to set up a sign operation.
        extern "C" fn C_SignInit(
            hSession: CK_SESSION_HANDLE,
            pMechanism: CK_MECHANISM_PTR,
            hKey: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            if pMechanism.is_null() {
                log_with_thread_id!(error, "C_SignInit: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            // Presumably we should validate the mechanism against hKey, but the specification
            // doesn't actually seem to require this.
            let mechanism = unsafe { *pMechanism };
            log_with_thread_id!(debug, "C_SignInit: mechanism is {:?}", mechanism);
            let mechanism_params = if mechanism.mechanism == CKM_RSA_PKCS_PSS {
                if mechanism.ulParameterLen as usize
                    != std::mem::size_of::<CK_RSA_PKCS_PSS_PARAMS>()
                {
                    let len = mechanism.ulParameterLen;
                    log_with_thread_id!(
                        error,
                        "C_SignInit: bad ulParameterLen for CKM_RSA_PKCS_PSS: {}",
                        len
                    );
                    return CKR_ARGUMENTS_BAD;
                }
                Some(unsafe { *(mechanism.pParameter as *const CK_RSA_PKCS_PSS_PARAMS) })
            } else {
                None
            };
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            match manager.start_sign(hSession, hKey, mechanism_params) {
                Ok(()) => {}
                Err(e) => {
                    log_with_thread_id!(error, "C_SignInit: CKR_GENERAL_ERROR: {}", e);
                    return CKR_GENERAL_ERROR;
                }
            };
            log_with_thread_id!(debug, "C_SignInit: CKR_OK");
            CKR_OK
        }

        /// NSS calls this after `C_SignInit` (there are more ways in the PKCS #11 specification to
        /// sign data, but this is the only way supported by these modules).
        extern "C" fn C_Sign(
            hSession: CK_SESSION_HANDLE,
            pData: CK_BYTE_PTR,
            ulDataLen: CK_ULONG,
            pSignature: CK_BYTE_PTR,
            pulSignatureLen: CK_ULONG_PTR,
        ) -> CK_RV {
            if pData.is_null() || pulSignatureLen.is_null() {
                log_with_thread_id!(error, "C_Sign: CKR_ARGUMENTS_BAD");
                return CKR_ARGUMENTS_BAD;
            }
            let data = unsafe { std::slice::from_raw_parts(pData, ulDataLen as usize) };
            if pSignature.is_null() {
                let mut manager_guard = try_to_get_manager_guard!();
                let manager = manager_guard_to_manager!(manager_guard);
                match manager.get_signature_length(hSession, data.to_vec()) {
                    Ok(signature_length) => unsafe {
                        *pulSignatureLen = signature_length as CK_ULONG;
                    },
                    Err(e) => {
                        log_with_thread_id!(error, "C_Sign: get_signature_length failed: {}", e);
                        return CKR_GENERAL_ERROR;
                    }
                }
            } else {
                let mut manager_guard = try_to_get_manager_guard!();
                let manager = manager_guard_to_manager!(manager_guard);
                match manager.sign(hSession, data.to_vec()) {
                    Ok(signature) => {
                        let signature_capacity = unsafe { *pulSignatureLen } as usize;
                        if signature_capacity < signature.len() {
                            log_with_thread_id!(error, "C_Sign: CKR_ARGUMENTS_BAD");
                            return CKR_ARGUMENTS_BAD;
                        }
                        let ptr: *mut u8 = pSignature as *mut u8;
                        unsafe {
                            std::ptr::copy_nonoverlapping(signature.as_ptr(), ptr, signature.len());
                            *pulSignatureLen = signature.len() as CK_ULONG;
                        }
                    }
                    Err(e) => {
                        log_with_thread_id!(error, "C_Sign: sign failed: {}", e);
                        return CKR_GENERAL_ERROR;
                    }
                }
            }
            log_with_thread_id!(debug, "C_Sign: CKR_OK");
            CKR_OK
        }
    };
}

#[macro_export]
macro_rules! declare_pkcs11_pin_functions {
    () => {
        extern "C" fn C_InitPIN(
            hSession: CK_SESSION_HANDLE,
            pPin: CK_UTF8CHAR_PTR,
            ulPinLen: CK_ULONG,
        ) -> CK_RV {
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let pin = unsafe { rsclientcerts::cryptoki::char_ptr_to_slice(pPin, ulPinLen) };
            match manager.change_password(hSession, &[], pin) {
                Ok(()) => {
                    log_with_thread_id!(debug, "C_InitPIN: CKR_OK");
                    CKR_OK
                }
                Err(e) => {
                    log_with_thread_id!(error, "C_InitPIN failed: {}", e);
                    CKR_GENERAL_ERROR
                }
            }
        }

        extern "C" fn C_SetPIN(
            hSession: CK_SESSION_HANDLE,
            pOldPin: CK_UTF8CHAR_PTR,
            ulOldLen: CK_ULONG,
            pNewPin: CK_UTF8CHAR_PTR,
            ulNewLen: CK_ULONG,
        ) -> CK_RV {
            let mut manager_guard = try_to_get_manager_guard!();
            let manager = manager_guard_to_manager!(manager_guard);
            let from_pin = unsafe { rsclientcerts::cryptoki::char_ptr_to_slice(pOldPin, ulOldLen) };
            let to_pin = unsafe { rsclientcerts::cryptoki::char_ptr_to_slice(pNewPin, ulNewLen) };
            match manager.change_password(hSession, from_pin, to_pin) {
                Ok(()) => {
                    log_with_thread_id!(debug, "C_SetPIN: CKR_OK");
                    CKR_OK
                }
                Err(e) => {
                    log_with_thread_id!(error, "C_SetPIN failed: {}", e);
                    CKR_PIN_INCORRECT
                }
            }
        }
    };
}

#[macro_export]
macro_rules! declare_unsupported_pkcs11_functions {
    () => {
        extern "C" fn C_GetMechanismInfo(
            _slotID: CK_SLOT_ID,
            _type: CK_MECHANISM_TYPE,
            _pInfo: CK_MECHANISM_INFO_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_GetMechanismInfo: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_InitToken(
            _slotID: CK_SLOT_ID,
            _pPin: CK_UTF8CHAR_PTR,
            _ulPinLen: CK_ULONG,
            _pLabel: CK_UTF8CHAR_PTR,
        ) -> CK_RV {
            log_with_thread_id!(debug, "C_InitToken: CKR_OK");
            CKR_OK
        }

        extern "C" fn C_GetOperationState(
            _hSession: CK_SESSION_HANDLE,
            _pOperationState: CK_BYTE_PTR,
            _pulOperationStateLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_GetOperationState: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SetOperationState(
            _hSession: CK_SESSION_HANDLE,
            _pOperationState: CK_BYTE_PTR,
            _ulOperationStateLen: CK_ULONG,
            _hEncryptionKey: CK_OBJECT_HANDLE,
            _hAuthenticationKey: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SetOperationState: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_CreateObject(
            _hSession: CK_SESSION_HANDLE,
            _pTemplate: CK_ATTRIBUTE_PTR,
            _ulCount: CK_ULONG,
            _phObject: CK_OBJECT_HANDLE_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_InitPIN: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_CopyObject(
            _hSession: CK_SESSION_HANDLE,
            _hObject: CK_OBJECT_HANDLE,
            _pTemplate: CK_ATTRIBUTE_PTR,
            _ulCount: CK_ULONG,
            _phNewObject: CK_OBJECT_HANDLE_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_CopyObject: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DestroyObject(
            _hSession: CK_SESSION_HANDLE,
            _hObject: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DestroyObject: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_GetObjectSize(
            _hSession: CK_SESSION_HANDLE,
            _hObject: CK_OBJECT_HANDLE,
            _pulSize: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_GetObjectSize: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SetAttributeValue(
            _hSession: CK_SESSION_HANDLE,
            _hObject: CK_OBJECT_HANDLE,
            _pTemplate: CK_ATTRIBUTE_PTR,
            _ulCount: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SetAttributeValue: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_EncryptInit(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hKey: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_EncryptInit: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_Encrypt(
            _hSession: CK_SESSION_HANDLE,
            _pData: CK_BYTE_PTR,
            _ulDataLen: CK_ULONG,
            _pEncryptedData: CK_BYTE_PTR,
            _pulEncryptedDataLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_Encrypt: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_EncryptUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pPart: CK_BYTE_PTR,
            _ulPartLen: CK_ULONG,
            _pEncryptedPart: CK_BYTE_PTR,
            _pulEncryptedPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_EncryptUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_EncryptFinal(
            _hSession: CK_SESSION_HANDLE,
            _pLastEncryptedPart: CK_BYTE_PTR,
            _pulLastEncryptedPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_EncryptFinal: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DecryptInit(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hKey: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DecryptInit: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_Decrypt(
            _hSession: CK_SESSION_HANDLE,
            _pEncryptedData: CK_BYTE_PTR,
            _ulEncryptedDataLen: CK_ULONG,
            _pData: CK_BYTE_PTR,
            _pulDataLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_Decrypt: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DecryptUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pEncryptedPart: CK_BYTE_PTR,
            _ulEncryptedPartLen: CK_ULONG,
            _pPart: CK_BYTE_PTR,
            _pulPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DecryptUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DecryptFinal(
            _hSession: CK_SESSION_HANDLE,
            _pLastPart: CK_BYTE_PTR,
            _pulLastPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DecryptFinal: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DigestInit(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DigestInit: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_Digest(
            _hSession: CK_SESSION_HANDLE,
            _pData: CK_BYTE_PTR,
            _ulDataLen: CK_ULONG,
            _pDigest: CK_BYTE_PTR,
            _pulDigestLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_Digest: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DigestUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pPart: CK_BYTE_PTR,
            _ulPartLen: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DigestUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DigestKey(_hSession: CK_SESSION_HANDLE, _hKey: CK_OBJECT_HANDLE) -> CK_RV {
            log_with_thread_id!(error, "C_DigestKey: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DigestFinal(
            _hSession: CK_SESSION_HANDLE,
            _pDigest: CK_BYTE_PTR,
            _pulDigestLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DigestFinal: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SignUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pPart: CK_BYTE_PTR,
            _ulPartLen: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SignUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SignFinal(
            _hSession: CK_SESSION_HANDLE,
            _pSignature: CK_BYTE_PTR,
            _pulSignatureLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SignFinal: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SignRecoverInit(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hKey: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SignRecoverInit: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SignRecover(
            _hSession: CK_SESSION_HANDLE,
            _pData: CK_BYTE_PTR,
            _ulDataLen: CK_ULONG,
            _pSignature: CK_BYTE_PTR,
            _pulSignatureLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SignRecover: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_VerifyInit(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hKey: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_VerifyInit: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_Verify(
            _hSession: CK_SESSION_HANDLE,
            _pData: CK_BYTE_PTR,
            _ulDataLen: CK_ULONG,
            _pSignature: CK_BYTE_PTR,
            _ulSignatureLen: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_Verify: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_VerifyUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pPart: CK_BYTE_PTR,
            _ulPartLen: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_VerifyUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_VerifyFinal(
            _hSession: CK_SESSION_HANDLE,
            _pSignature: CK_BYTE_PTR,
            _ulSignatureLen: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_VerifyFinal: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_VerifyRecoverInit(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hKey: CK_OBJECT_HANDLE,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_VerifyRecoverInit: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_VerifyRecover(
            _hSession: CK_SESSION_HANDLE,
            _pSignature: CK_BYTE_PTR,
            _ulSignatureLen: CK_ULONG,
            _pData: CK_BYTE_PTR,
            _pulDataLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_VerifyRecover: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DigestEncryptUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pPart: CK_BYTE_PTR,
            _ulPartLen: CK_ULONG,
            _pEncryptedPart: CK_BYTE_PTR,
            _pulEncryptedPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DigestEncryptUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DecryptDigestUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pEncryptedPart: CK_BYTE_PTR,
            _ulEncryptedPartLen: CK_ULONG,
            _pPart: CK_BYTE_PTR,
            _pulPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DecryptDigestUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SignEncryptUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pPart: CK_BYTE_PTR,
            _ulPartLen: CK_ULONG,
            _pEncryptedPart: CK_BYTE_PTR,
            _pulEncryptedPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SignEncryptUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DecryptVerifyUpdate(
            _hSession: CK_SESSION_HANDLE,
            _pEncryptedPart: CK_BYTE_PTR,
            _ulEncryptedPartLen: CK_ULONG,
            _pPart: CK_BYTE_PTR,
            _pulPartLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DecryptVerifyUpdate: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_GenerateKey(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _pTemplate: CK_ATTRIBUTE_PTR,
            _ulCount: CK_ULONG,
            _phKey: CK_OBJECT_HANDLE_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_GenerateKey: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_GenerateKeyPair(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _pPublicKeyTemplate: CK_ATTRIBUTE_PTR,
            _ulPublicKeyAttributeCount: CK_ULONG,
            _pPrivateKeyTemplate: CK_ATTRIBUTE_PTR,
            _ulPrivateKeyAttributeCount: CK_ULONG,
            _phPublicKey: CK_OBJECT_HANDLE_PTR,
            _phPrivateKey: CK_OBJECT_HANDLE_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_GenerateKeyPair: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_WrapKey(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hWrappingKey: CK_OBJECT_HANDLE,
            _hKey: CK_OBJECT_HANDLE,
            _pWrappedKey: CK_BYTE_PTR,
            _pulWrappedKeyLen: CK_ULONG_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_WrapKey: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_UnwrapKey(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hUnwrappingKey: CK_OBJECT_HANDLE,
            _pWrappedKey: CK_BYTE_PTR,
            _ulWrappedKeyLen: CK_ULONG,
            _pTemplate: CK_ATTRIBUTE_PTR,
            _ulAttributeCount: CK_ULONG,
            _phKey: CK_OBJECT_HANDLE_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_UnwrapKey: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_DeriveKey(
            _hSession: CK_SESSION_HANDLE,
            _pMechanism: CK_MECHANISM_PTR,
            _hBaseKey: CK_OBJECT_HANDLE,
            _pTemplate: CK_ATTRIBUTE_PTR,
            _ulAttributeCount: CK_ULONG,
            _phKey: CK_OBJECT_HANDLE_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_DeriveKey: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_SeedRandom(
            _hSession: CK_SESSION_HANDLE,
            _pSeed: CK_BYTE_PTR,
            _ulSeedLen: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_SeedRandom: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_GenerateRandom(
            _hSession: CK_SESSION_HANDLE,
            _RandomData: CK_BYTE_PTR,
            _ulRandomLen: CK_ULONG,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_GenerateRandom: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_GetFunctionStatus(_hSession: CK_SESSION_HANDLE) -> CK_RV {
            log_with_thread_id!(error, "C_GetFunctionStatus: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_CancelFunction(_hSession: CK_SESSION_HANDLE) -> CK_RV {
            log_with_thread_id!(error, "C_CancelFunction: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }

        extern "C" fn C_WaitForSlotEvent(
            _flags: CK_FLAGS,
            _pSlot: CK_SLOT_ID_PTR,
            _pRserved: CK_VOID_PTR,
        ) -> CK_RV {
            log_with_thread_id!(error, "C_WaitForSlotEvent: CKR_FUNCTION_NOT_SUPPORTED");
            CKR_FUNCTION_NOT_SUPPORTED
        }
    };
}
