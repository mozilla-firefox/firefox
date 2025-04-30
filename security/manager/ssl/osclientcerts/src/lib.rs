/* -*- Mode: rust; rust-indent-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(non_snake_case)]

extern crate byteorder;
#[cfg(any(target_os = "macos", target_os = "ios"))]
#[macro_use]
extern crate core_foundation;
extern crate env_logger;
#[cfg(any(target_os = "macos", target_os = "ios"))]
#[macro_use]
extern crate lazy_static;
#[cfg(target_os = "macos")]
extern crate libloading;
#[macro_use]
extern crate log;
extern crate pkcs11_bindings;
#[macro_use]
extern crate rsclientcerts;
extern crate sha2;
#[cfg(all(target_os = "windows", not(target_arch = "aarch64")))]
extern crate winapi;

use pkcs11_bindings::*;
use rsclientcerts::manager::ManagerProxy;
use std::convert::TryInto;
use std::sync::Mutex;
use std::thread;

#[cfg(target_os = "android")]
mod backend_android;
#[cfg(any(target_os = "macos", target_os = "ios"))]
mod backend_macos;
#[cfg(all(target_os = "windows", not(target_arch = "aarch64")))]
mod backend_windows;

#[cfg(target_os = "android")]
use crate::backend_android::Backend;
#[cfg(any(target_os = "macos", target_os = "ios"))]
use crate::backend_macos::Backend;
#[cfg(all(target_os = "windows", not(target_arch = "aarch64")))]
use crate::backend_windows::Backend;

/// The singleton `ManagerProxy` that handles state with respect to PKCS #11. Only one thread
/// may use it at a time, but there is no restriction on which threads may use it. However, as
/// OS APIs being used are not necessarily thread-safe (e.g. they may be using
/// thread-local-storage), the `ManagerProxy` forwards calls from any thread
/// to a single thread where the real `Manager` does the actual work.
static MANAGER_PROXY: Mutex<Option<ManagerProxy>> = Mutex::new(None);

// Obtaining a handle on the manager proxy is a two-step process. First the mutex must be locked,
// which (if successful), results in a mutex guard object. We must then get a mutable refence to the
// underlying manager proxy (if set - otherwise we return an error). This can't happen all in one
// macro without dropping a reference that needs to live long enough for this to be safe. In
// practice, this looks like:
//   let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
//   let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
macro_rules! try_to_get_manager_proxy_guard {
    () => {
        match MANAGER_PROXY.lock() {
            Ok(maybe_manager_proxy) => maybe_manager_proxy,
            Err(poison_error) => {
                log_with_thread_id!(
                    error,
                    "previous thread panicked acquiring manager lock: {}",
                    poison_error
                );
                return CKR_DEVICE_ERROR;
            }
        }
    };
}

macro_rules! manager_proxy_guard_to_manager {
    ($manager_proxy_guard:ident) => {
        match $manager_proxy_guard.as_mut() {
            Some(manager_proxy) => manager_proxy,
            None => {
                log_with_thread_id!(error, "module state expected to be set, but it is not");
                return CKR_DEVICE_ERROR;
            }
        }
    };
}

// Helper macro to prefix log messages with the current thread ID.
macro_rules! log_with_thread_id {
    ($log_level:ident, $($message:expr),*) => {
        $log_level!("{:?} {}", thread::current().id(), format_args!($($message),*));
    };
}

/// This gets called to initialize the module. For this implementation, this consists of
/// instantiating the `ManagerProxy`.
extern "C" fn C_Initialize(_pInitArgs: CK_VOID_PTR) -> CK_RV {
    // This will fail if this has already been called, but this isn't a problem because either way,
    // logging has been initialized.
    let _ = env_logger::try_init();

    #[cfg(target_os = "android")]
    {
        android_logger::init_once(
            android_logger::Config::default().with_max_level(log::LevelFilter::Trace),
        );
    }

    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager_proxy = match ManagerProxy::new("osclientcerts", Backend {}) {
        Ok(p) => p,
        Err(e) => {
            log_with_thread_id!(error, "C_Initialize: ManagerProxy: {}", e);
            return CKR_DEVICE_ERROR;
        }
    };
    match manager_proxy_guard.replace(manager_proxy) {
        Some(_unexpected_previous_manager_proxy) => {
            log_with_thread_id!(
        warn,
        "C_Initialize: replacing previously set module state (this is expected on macOS but not on Windows)"
    );
        }
        None => {}
    }
    log_with_thread_id!(debug, "C_Initialize: CKR_OK");
    CKR_OK
}

extern "C" fn C_Finalize(_pReserved: CK_VOID_PTR) -> CK_RV {
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
    match manager.stop() {
        Ok(()) => {
            log_with_thread_id!(debug, "C_Finalize: CKR_OK");
            CKR_OK
        }
        Err(e) => {
            log_with_thread_id!(error, "C_Finalize: CKR_DEVICE_ERROR: {}", e);
            CKR_DEVICE_ERROR
        }
    }
}

// The specification mandates that these strings be padded with spaces to the appropriate length.
// Since the length of fixed-size arrays in rust is part of the type, the compiler enforces that
// these byte strings are of the correct length.
const MANUFACTURER_ID_BYTES: &[u8; 32] = b"Mozilla Corporation             ";
const LIBRARY_DESCRIPTION_BYTES: &[u8; 32] = b"OS Client Cert Module           ";

/// This gets called to gather some information about the module. In particular, this implementation
/// supports (portions of) cryptoki (PKCS #11) version 2.2.
extern "C" fn C_GetInfo(pInfo: CK_INFO_PTR) -> CK_RV {
    if pInfo.is_null() {
        log_with_thread_id!(error, "C_GetInfo: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    log_with_thread_id!(debug, "C_GetInfo: CKR_OK");
    let mut info = CK_INFO::default();
    info.cryptokiVersion.major = 2;
    info.cryptokiVersion.minor = 2;
    info.manufacturerID = *MANUFACTURER_ID_BYTES;
    info.libraryDescription = *LIBRARY_DESCRIPTION_BYTES;
    unsafe {
        *pInfo = info;
    }
    CKR_OK
}

/// This module has one slot.
const SLOT_COUNT: CK_ULONG = 1;
const SLOT_ID: CK_SLOT_ID = 1;

/// This gets called twice: once with a null `pSlotList` to get the number of slots (returned via
/// `pulCount`) and a second time to get the ID for each slot.
extern "C" fn C_GetSlotList(
    _tokenPresent: CK_BBOOL,
    pSlotList: CK_SLOT_ID_PTR,
    pulCount: CK_ULONG_PTR,
) -> CK_RV {
    if pulCount.is_null() {
        log_with_thread_id!(error, "C_GetSlotList: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    if !pSlotList.is_null() {
        if unsafe { *pulCount } < SLOT_COUNT {
            log_with_thread_id!(error, "C_GetSlotList: CKR_BUFFER_TOO_SMALL");
            return CKR_BUFFER_TOO_SMALL;
        }
        unsafe {
            *pSlotList = SLOT_ID;
        }
    };
    unsafe {
        *pulCount = SLOT_COUNT;
    }
    log_with_thread_id!(debug, "C_GetSlotList: CKR_OK");
    CKR_OK
}

const SLOT_DESCRIPTION_BYTES: &[u8; 64] =
    b"OS Client Cert Slot                                             ";

/// This gets called to obtain information about slots. In this implementation, the token is
/// always present in the singular slot.
extern "C" fn C_GetSlotInfo(slotID: CK_SLOT_ID, pInfo: CK_SLOT_INFO_PTR) -> CK_RV {
    if slotID != SLOT_ID || pInfo.is_null() {
        log_with_thread_id!(error, "C_GetSlotInfo: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    let slot_info = CK_SLOT_INFO {
        slotDescription: *SLOT_DESCRIPTION_BYTES,
        manufacturerID: *MANUFACTURER_ID_BYTES,
        flags: CKF_TOKEN_PRESENT,
        hardwareVersion: CK_VERSION::default(),
        firmwareVersion: CK_VERSION::default(),
    };
    unsafe {
        *pInfo = slot_info;
    }
    log_with_thread_id!(debug, "C_GetSlotInfo: CKR_OK");
    CKR_OK
}

const TOKEN_LABEL_BYTES: &[u8; 32] = b"OS Client Cert Token            ";
const TOKEN_MODEL_BYTES: &[u8; 16] = b"osclientcerts   ";
const TOKEN_SERIAL_NUMBER_BYTES: &[u8; 16] = b"0000000000000000";

/// This gets called to obtain some information about tokens. This implementation has one slot,
/// so it has one token. This information is primarily for display purposes.
extern "C" fn C_GetTokenInfo(slotID: CK_SLOT_ID, pInfo: CK_TOKEN_INFO_PTR) -> CK_RV {
    if slotID != SLOT_ID || pInfo.is_null() {
        log_with_thread_id!(error, "C_GetTokenInfo: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    let mut token_info = CK_TOKEN_INFO::default();
    token_info.label = *TOKEN_LABEL_BYTES;
    token_info.manufacturerID = *MANUFACTURER_ID_BYTES;
    token_info.model = *TOKEN_MODEL_BYTES;
    token_info.serialNumber = *TOKEN_SERIAL_NUMBER_BYTES;
    unsafe {
        *pInfo = token_info;
    }
    log_with_thread_id!(debug, "C_GetTokenInfo: CKR_OK");
    CKR_OK
}

/// This gets called to determine what mechanisms a slot supports. The singular slot supports
/// ECDSA and RSA PKCS1. Depending on the configuration the module was loaded with, it may also
/// support RSA PSS.
extern "C" fn C_GetMechanismList(
    slotID: CK_SLOT_ID,
    pMechanismList: CK_MECHANISM_TYPE_PTR,
    pulCount: CK_ULONG_PTR,
) -> CK_RV {
    if slotID != SLOT_ID || pulCount.is_null() {
        log_with_thread_id!(error, "C_GetMechanismList: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    let mechanisms = &[CKM_ECDSA, CKM_RSA_PKCS, CKM_RSA_PKCS_PSS];
    let mechanisms_len: CK_ULONG = mechanisms.len().try_into().unwrap();
    if !pMechanismList.is_null() {
        if unsafe { *pulCount } < mechanisms_len {
            log_with_thread_id!(error, "C_GetMechanismList: CKR_ARGUMENTS_BAD");
            return CKR_ARGUMENTS_BAD;
        }
        let mechanism_list =
            unsafe { std::slice::from_raw_parts_mut(pMechanismList, mechanisms.len()) };
        mechanism_list.copy_from_slice(mechanisms);
    }
    unsafe {
        *pulCount = mechanisms_len;
    }
    log_with_thread_id!(debug, "C_GetMechanismList: CKR_OK");
    CKR_OK
}

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
    log_with_thread_id!(error, "C_InitToken: CKR_FUNCTION_NOT_SUPPORTED");
    CKR_FUNCTION_NOT_SUPPORTED
}

extern "C" fn C_InitPIN(
    _hSession: CK_SESSION_HANDLE,
    _pPin: CK_UTF8CHAR_PTR,
    _ulPinLen: CK_ULONG,
) -> CK_RV {
    log_with_thread_id!(error, "C_InitPIN: CKR_FUNCTION_NOT_SUPPORTED");
    CKR_FUNCTION_NOT_SUPPORTED
}

extern "C" fn C_SetPIN(
    _hSession: CK_SESSION_HANDLE,
    _pOldPin: CK_UTF8CHAR_PTR,
    _ulOldLen: CK_ULONG,
    _pNewPin: CK_UTF8CHAR_PTR,
    _ulNewLen: CK_ULONG,
) -> CK_RV {
    log_with_thread_id!(error, "C_SetPIN: CKR_FUNCTION_NOT_SUPPORTED");
    CKR_FUNCTION_NOT_SUPPORTED
}

/// This gets called to create a new session. This module defers to the `ManagerProxy` to implement
/// this.
extern "C" fn C_OpenSession(
    slotID: CK_SLOT_ID,
    _flags: CK_FLAGS,
    _pApplication: CK_VOID_PTR,
    _Notify: CK_NOTIFY,
    phSession: CK_SESSION_HANDLE_PTR,
) -> CK_RV {
    if slotID != SLOT_ID || phSession.is_null() {
        log_with_thread_id!(error, "C_OpenSession: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
    let session_handle = match manager.open_session() {
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

/// This gets called to close a session. This is handled by the `ManagerProxy`.
extern "C" fn C_CloseSession(hSession: CK_SESSION_HANDLE) -> CK_RV {
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
    if manager.close_session(hSession).is_err() {
        log_with_thread_id!(error, "C_CloseSession: CKR_SESSION_HANDLE_INVALID");
        return CKR_SESSION_HANDLE_INVALID;
    }
    log_with_thread_id!(debug, "C_CloseSession: CKR_OK");
    CKR_OK
}

/// This gets called to close all open sessions at once. This is handled by the `ManagerProxy`.
extern "C" fn C_CloseAllSessions(slotID: CK_SLOT_ID) -> CK_RV {
    if slotID != SLOT_ID {
        log_with_thread_id!(error, "C_CloseAllSessions: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
    match manager.close_all_sessions() {
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

extern "C" fn C_GetSessionInfo(_hSession: CK_SESSION_HANDLE, _pInfo: CK_SESSION_INFO_PTR) -> CK_RV {
    log_with_thread_id!(error, "C_GetSessionInfo: CKR_FUNCTION_NOT_SUPPORTED");
    CKR_FUNCTION_NOT_SUPPORTED
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

extern "C" fn C_Login(
    _hSession: CK_SESSION_HANDLE,
    _userType: CK_USER_TYPE,
    _pPin: CK_UTF8CHAR_PTR,
    _ulPinLen: CK_ULONG,
) -> CK_RV {
    log_with_thread_id!(error, "C_Login: CKR_FUNCTION_NOT_SUPPORTED");
    CKR_FUNCTION_NOT_SUPPORTED
}

/// This gets called to log out and drop any authenticated resources. Because this module does not
/// hold on to authenticated resources, this module "implements" this by doing nothing and
/// returning a success result.
extern "C" fn C_Logout(_hSession: CK_SESSION_HANDLE) -> CK_RV {
    log_with_thread_id!(debug, "C_Logout: CKR_OK");
    CKR_OK
}

extern "C" fn C_CreateObject(
    _hSession: CK_SESSION_HANDLE,
    _pTemplate: CK_ATTRIBUTE_PTR,
    _ulCount: CK_ULONG,
    _phObject: CK_OBJECT_HANDLE_PTR,
) -> CK_RV {
    log_with_thread_id!(error, "C_CreateObject: CKR_FUNCTION_NOT_SUPPORTED");
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

extern "C" fn C_DestroyObject(_hSession: CK_SESSION_HANDLE, _hObject: CK_OBJECT_HANDLE) -> CK_RV {
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

/// This gets called to obtain the values of a number of attributes of an object identified by the
/// given handle. This module implements this by requesting that the `ManagerProxy` find the object
/// and attempt to get the value of each attribute. If a specified attribute is not defined on the
/// object, the length of that attribute is set to -1 to indicate that it is not available.
/// This gets called twice: once to obtain the lengths of the attributes and again to get the
/// values.
extern "C" fn C_GetAttributeValue(
    _hSession: CK_SESSION_HANDLE,
    hObject: CK_OBJECT_HANDLE,
    pTemplate: CK_ATTRIBUTE_PTR,
    ulCount: CK_ULONG,
) -> CK_RV {
    if pTemplate.is_null() {
        log_with_thread_id!(error, "C_GetAttributeValue: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    let mut attr_types = Vec::with_capacity(ulCount as usize);
    for i in 0..ulCount as usize {
        let attr = unsafe { &*pTemplate.add(i) };
        attr_types.push(attr.type_);
    }
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
    let values = match manager.get_attributes(hObject, attr_types) {
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
            if attr.pValue.is_null() {
                attr.ulValueLen = attr_value.len() as CK_ULONG;
            } else {
                let ptr: *mut u8 = attr.pValue as *mut u8;
                if attr_value.len() != attr.ulValueLen as usize {
                    log_with_thread_id!(error, "C_GetAttributeValue: incorrect attr size");
                    return CKR_ARGUMENTS_BAD;
                }
                unsafe {
                    std::ptr::copy_nonoverlapping(attr_value.as_ptr(), ptr, attr_value.len());
                }
            }
        } else {
            attr.ulValueLen = (0 - 1) as CK_ULONG;
        }
    }
    log_with_thread_id!(debug, "C_GetAttributeValue: CKR_OK");
    CKR_OK
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

fn trace_attr(prefix: &str, attr: &CK_ATTRIBUTE) {
    let typ = match unsafe_packed_field_access!(attr.type_) {
        CKA_CLASS => "CKA_CLASS".to_string(),
        CKA_TOKEN => "CKA_TOKEN".to_string(),
        CKA_LABEL => "CKA_LABEL".to_string(),
        CKA_ID => "CKA_ID".to_string(),
        CKA_VALUE => "CKA_VALUE".to_string(),
        CKA_ISSUER => "CKA_ISSUER".to_string(),
        CKA_SERIAL_NUMBER => "CKA_SERIAL_NUMBER".to_string(),
        CKA_SUBJECT => "CKA_SUBJECT".to_string(),
        CKA_PRIVATE => "CKA_PRIVATE".to_string(),
        CKA_KEY_TYPE => "CKA_KEY_TYPE".to_string(),
        CKA_MODULUS => "CKA_MODULUS".to_string(),
        CKA_EC_PARAMS => "CKA_EC_PARAMS".to_string(),
        _ => format!("0x{:x}", unsafe_packed_field_access!(attr.type_)),
    };
    let value =
        unsafe { std::slice::from_raw_parts(attr.pValue as *const u8, attr.ulValueLen as usize) };
    log_with_thread_id!(
        trace,
        "{}CK_ATTRIBUTE {{ type: {}, pValue: {:?}, ulValueLen: {} }}",
        prefix,
        typ,
        value,
        unsafe_packed_field_access!(attr.ulValueLen)
    );
}

const RELEVANT_ATTRIBUTES: &[CK_ATTRIBUTE_TYPE] = &[
    CKA_CLASS,
    CKA_EC_PARAMS,
    CKA_ID,
    CKA_ISSUER,
    CKA_KEY_TYPE,
    CKA_LABEL,
    CKA_MODULUS,
    CKA_PRIVATE,
    CKA_SERIAL_NUMBER,
    CKA_SUBJECT,
    CKA_TOKEN,
    CKA_VALUE,
];

/// This gets called to initialize a search for objects matching a given list of attributes. This
/// module implements this by gathering the attributes and passing them to the `ManagerProxy` to
/// start the search.
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
        trace_attr("  ", attr);
        // Copy out the attribute type to avoid making a reference to an unaligned field.
        let attr_type = attr.type_;
        if !RELEVANT_ATTRIBUTES.contains(&attr_type) {
            log_with_thread_id!(
                debug,
                "C_FindObjectsInit: irrelevant attribute, returning CKR_ATTRIBUTE_TYPE_INVALID"
            );
            return CKR_ATTRIBUTE_TYPE_INVALID;
        }
        let slice = unsafe {
            std::slice::from_raw_parts(attr.pValue as *const u8, attr.ulValueLen as usize)
        };
        attrs.push((attr_type, slice.to_owned()));
    }
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
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

/// This gets called after `C_FindObjectsInit` to get the results of a search. This module
/// implements this by looking up the search in the `ManagerProxy` and copying out the matching
/// object handles.
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
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
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

/// This gets called after `C_FindObjectsInit` and `C_FindObjects` to finish a search. The module
/// tells the `ManagerProxy` to clear the search.
extern "C" fn C_FindObjectsFinal(hSession: CK_SESSION_HANDLE) -> CK_RV {
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
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

extern "C" fn C_DigestInit(_hSession: CK_SESSION_HANDLE, _pMechanism: CK_MECHANISM_PTR) -> CK_RV {
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

/// This gets called to set up a sign operation. The module essentially defers to the
/// `ManagerProxy`.
extern "C" fn C_SignInit(
    hSession: CK_SESSION_HANDLE,
    pMechanism: CK_MECHANISM_PTR,
    hKey: CK_OBJECT_HANDLE,
) -> CK_RV {
    if pMechanism.is_null() {
        log_with_thread_id!(error, "C_SignInit: CKR_ARGUMENTS_BAD");
        return CKR_ARGUMENTS_BAD;
    }
    // Presumably we should validate the mechanism against hKey, but the specification doesn't
    // actually seem to require this.
    let mechanism = unsafe { *pMechanism };
    log_with_thread_id!(debug, "C_SignInit: mechanism is {:?}", mechanism);
    let mechanism_params = if mechanism.mechanism == CKM_RSA_PKCS_PSS {
        if mechanism.ulParameterLen as usize != std::mem::size_of::<CK_RSA_PKCS_PSS_PARAMS>() {
            log_with_thread_id!(
                error,
                "C_SignInit: bad ulParameterLen for CKM_RSA_PKCS_PSS: {}",
                unsafe_packed_field_access!(mechanism.ulParameterLen)
            );
            return CKR_ARGUMENTS_BAD;
        }
        Some(unsafe { *(mechanism.pParameter as *const CK_RSA_PKCS_PSS_PARAMS) })
    } else {
        None
    };
    let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
    let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
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

/// NSS calls this after `C_SignInit` (there are more ways in the PKCS #11 specification to sign
/// data, but this is the only way supported by this module). The module essentially defers to the
/// `ManagerProxy` and copies out the resulting signature.
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
        let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
        let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
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
        let mut manager_proxy_guard = try_to_get_manager_proxy_guard!();
        let manager = manager_proxy_guard_to_manager!(manager_proxy_guard);
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

/// To be a valid PKCS #11 module, this list of functions must be supported. At least cryptoki 2.2
/// must be supported for this module to work in NSS.
static FUNCTION_LIST: CK_FUNCTION_LIST = CK_FUNCTION_LIST {
    version: CK_VERSION { major: 2, minor: 2 },
    C_Initialize: Some(C_Initialize),
    C_Finalize: Some(C_Finalize),
    C_GetInfo: Some(C_GetInfo),
    C_GetFunctionList: None,
    C_GetSlotList: Some(C_GetSlotList),
    C_GetSlotInfo: Some(C_GetSlotInfo),
    C_GetTokenInfo: Some(C_GetTokenInfo),
    C_GetMechanismList: Some(C_GetMechanismList),
    C_GetMechanismInfo: Some(C_GetMechanismInfo),
    C_InitToken: Some(C_InitToken),
    C_InitPIN: Some(C_InitPIN),
    C_SetPIN: Some(C_SetPIN),
    C_OpenSession: Some(C_OpenSession),
    C_CloseSession: Some(C_CloseSession),
    C_CloseAllSessions: Some(C_CloseAllSessions),
    C_GetSessionInfo: Some(C_GetSessionInfo),
    C_GetOperationState: Some(C_GetOperationState),
    C_SetOperationState: Some(C_SetOperationState),
    C_Login: Some(C_Login),
    C_Logout: Some(C_Logout),
    C_CreateObject: Some(C_CreateObject),
    C_CopyObject: Some(C_CopyObject),
    C_DestroyObject: Some(C_DestroyObject),
    C_GetObjectSize: Some(C_GetObjectSize),
    C_GetAttributeValue: Some(C_GetAttributeValue),
    C_SetAttributeValue: Some(C_SetAttributeValue),
    C_FindObjectsInit: Some(C_FindObjectsInit),
    C_FindObjects: Some(C_FindObjects),
    C_FindObjectsFinal: Some(C_FindObjectsFinal),
    C_EncryptInit: Some(C_EncryptInit),
    C_Encrypt: Some(C_Encrypt),
    C_EncryptUpdate: Some(C_EncryptUpdate),
    C_EncryptFinal: Some(C_EncryptFinal),
    C_DecryptInit: Some(C_DecryptInit),
    C_Decrypt: Some(C_Decrypt),
    C_DecryptUpdate: Some(C_DecryptUpdate),
    C_DecryptFinal: Some(C_DecryptFinal),
    C_DigestInit: Some(C_DigestInit),
    C_Digest: Some(C_Digest),
    C_DigestUpdate: Some(C_DigestUpdate),
    C_DigestKey: Some(C_DigestKey),
    C_DigestFinal: Some(C_DigestFinal),
    C_SignInit: Some(C_SignInit),
    C_Sign: Some(C_Sign),
    C_SignUpdate: Some(C_SignUpdate),
    C_SignFinal: Some(C_SignFinal),
    C_SignRecoverInit: Some(C_SignRecoverInit),
    C_SignRecover: Some(C_SignRecover),
    C_VerifyInit: Some(C_VerifyInit),
    C_Verify: Some(C_Verify),
    C_VerifyUpdate: Some(C_VerifyUpdate),
    C_VerifyFinal: Some(C_VerifyFinal),
    C_VerifyRecoverInit: Some(C_VerifyRecoverInit),
    C_VerifyRecover: Some(C_VerifyRecover),
    C_DigestEncryptUpdate: Some(C_DigestEncryptUpdate),
    C_DecryptDigestUpdate: Some(C_DecryptDigestUpdate),
    C_SignEncryptUpdate: Some(C_SignEncryptUpdate),
    C_DecryptVerifyUpdate: Some(C_DecryptVerifyUpdate),
    C_GenerateKey: Some(C_GenerateKey),
    C_GenerateKeyPair: Some(C_GenerateKeyPair),
    C_WrapKey: Some(C_WrapKey),
    C_UnwrapKey: Some(C_UnwrapKey),
    C_DeriveKey: Some(C_DeriveKey),
    C_SeedRandom: Some(C_SeedRandom),
    C_GenerateRandom: Some(C_GenerateRandom),
    C_GetFunctionStatus: Some(C_GetFunctionStatus),
    C_CancelFunction: Some(C_CancelFunction),
    C_WaitForSlotEvent: Some(C_WaitForSlotEvent),
};

/// # Safety
///
/// This is the only function this module exposes. NSS calls it to obtain the list of functions
/// comprising this module.
/// ppFunctionList must be a valid pointer.
#[no_mangle]
pub unsafe extern "C" fn OSClientCerts_C_GetFunctionList(
    ppFunctionList: CK_FUNCTION_LIST_PTR_PTR,
) -> CK_RV {
    if ppFunctionList.is_null() {
        return CKR_ARGUMENTS_BAD;
    }
    // CK_FUNCTION_LIST_PTR is a *mut CK_FUNCTION_LIST, but as per the
    // specification, the caller must treat it as *const CK_FUNCTION_LIST.
    *ppFunctionList = std::ptr::addr_of!(FUNCTION_LIST) as CK_FUNCTION_LIST_PTR;
    CKR_OK
}

#[cfg_attr(
    any(target_os = "macos", target_os = "ios"),
    link(name = "Security", kind = "framework")
)]
extern "C" {}
