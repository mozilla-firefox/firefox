/* -*- Mode: rust; rust-indent-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(non_camel_case_types)]

use pkcs11_bindings::*;
use rsclientcerts::error::{Error, ErrorType};
use rsclientcerts::manager::{ClientCertsBackend, CryptokiObject, Sign};
use rsclientcerts::util::*;
use sha2::{Digest, Sha256};
use std::convert::TryInto;
use std::ffi::{c_void, CStr, CString};
use std::ops::Deref;
use std::slice;
use winapi::shared::bcrypt::*;
use winapi::shared::minwindef::{DWORD, PBYTE};
use winapi::um::errhandlingapi::GetLastError;
use winapi::um::ncrypt::*;
use winapi::um::wincrypt::{HCRYPTHASH, HCRYPTPROV, *};

// winapi has some support for ncrypt.h, but not for this function.
extern "system" {
    fn NCryptSignHash(
        hKey: NCRYPT_KEY_HANDLE,
        pPaddingInfo: *mut c_void,
        pbHashValue: PBYTE,
        cbHashValue: DWORD,
        pbSignature: PBYTE,
        cbSignature: DWORD,
        pcbResult: *mut DWORD,
        dwFlags: DWORD,
    ) -> SECURITY_STATUS;
}

/// Given a `CERT_INFO`, tries to return the bytes of the subject distinguished name as formatted by
/// `CertNameToStrA` using the flag `CERT_SIMPLE_NAME_STR`. This is used as the label for the
/// certificate.
fn get_cert_subject_dn(cert_info: &CERT_INFO) -> Result<Vec<u8>, Error> {
    let mut cert_info_subject = cert_info.Subject;
    let subject_dn_len = unsafe {
        CertNameToStrA(
            X509_ASN_ENCODING,
            &mut cert_info_subject,
            CERT_SIMPLE_NAME_STR,
            std::ptr::null_mut(),
            0,
        )
    };
    // subject_dn_len includes the terminating null byte.
    let mut subject_dn_string_bytes: Vec<u8> = vec![0; subject_dn_len as usize];
    let subject_dn_len = unsafe {
        CertNameToStrA(
            X509_ASN_ENCODING,
            &mut cert_info_subject,
            CERT_SIMPLE_NAME_STR,
            subject_dn_string_bytes.as_mut_ptr() as *mut i8,
            subject_dn_string_bytes
                .len()
                .try_into()
                .map_err(|_| error_here!(ErrorType::ValueTooLarge))?,
        )
    };
    if subject_dn_len as usize != subject_dn_string_bytes.len() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(subject_dn_string_bytes)
}

/// Represents a certificate for which there exists a corresponding private key.
pub struct Cert {
    /// PKCS #11 object class. Will be `CKO_CERTIFICATE`.
    class: Vec<u8>,
    /// Whether or not this is on a token. Will be `CK_TRUE`.
    token: Vec<u8>,
    /// An identifier unique to this certificate. Must be the same as the ID for the private key.
    id: Vec<u8>,
    /// The bytes of a human-readable label for this certificate. Will be the subject DN.
    label: Vec<u8>,
    /// The DER bytes of the certificate.
    value: Vec<u8>,
    /// The DER bytes of the issuer distinguished name of the certificate.
    issuer: Vec<u8>,
    /// The DER bytes of the serial number of the certificate.
    serial_number: Vec<u8>,
    /// The DER bytes of the subject distinguished name of the certificate.
    subject: Vec<u8>,
}

impl Cert {
    fn new(cert_context: PCCERT_CONTEXT) -> Result<Cert, Error> {
        let cert = unsafe { &*cert_context };
        let cert_info = unsafe { &*cert.pCertInfo };
        let value =
            unsafe { slice::from_raw_parts(cert.pbCertEncoded, cert.cbCertEncoded as usize) };
        let value = value.to_vec();
        let id = Sha256::digest(&value).to_vec();
        let label = get_cert_subject_dn(cert_info)?;
        let (serial_number, issuer, subject) = read_encoded_certificate_identifiers(&value)?;
        Ok(Cert {
            class: serialize_uint(CKO_CERTIFICATE)?,
            token: serialize_uint(CK_TRUE)?,
            id,
            label,
            value,
            issuer,
            serial_number,
            subject,
        })
    }

    fn class(&self) -> &[u8] {
        &self.class
    }

    fn token(&self) -> &[u8] {
        &self.token
    }

    fn id(&self) -> &[u8] {
        &self.id
    }

    fn label(&self) -> &[u8] {
        &self.label
    }

    fn value(&self) -> &[u8] {
        &self.value
    }

    fn issuer(&self) -> &[u8] {
        &self.issuer
    }

    fn serial_number(&self) -> &[u8] {
        &self.serial_number
    }

    fn subject(&self) -> &[u8] {
        &self.subject
    }
}

impl CryptokiObject for Cert {
    fn matches(&self, attrs: &[(CK_ATTRIBUTE_TYPE, Vec<u8>)]) -> bool {
        for (attr_type, attr_value) in attrs {
            let comparison = match *attr_type {
                CKA_CLASS => self.class(),
                CKA_TOKEN => self.token(),
                CKA_LABEL => self.label(),
                CKA_ID => self.id(),
                CKA_VALUE => self.value(),
                CKA_ISSUER => self.issuer(),
                CKA_SERIAL_NUMBER => self.serial_number(),
                CKA_SUBJECT => self.subject(),
                _ => return false,
            };
            if attr_value.as_slice() != comparison {
                return false;
            }
        }
        true
    }

    fn get_attribute(&self, attribute: CK_ATTRIBUTE_TYPE) -> Option<&[u8]> {
        let result = match attribute {
            CKA_CLASS => self.class(),
            CKA_TOKEN => self.token(),
            CKA_LABEL => self.label(),
            CKA_ID => self.id(),
            CKA_VALUE => self.value(),
            CKA_ISSUER => self.issuer(),
            CKA_SERIAL_NUMBER => self.serial_number(),
            CKA_SUBJECT => self.subject(),
            _ => return None,
        };
        Some(result)
    }
}

struct CertContext(PCCERT_CONTEXT);

impl CertContext {
    fn new(cert: PCCERT_CONTEXT) -> CertContext {
        CertContext(unsafe { CertDuplicateCertificateContext(cert) })
    }
}

impl Drop for CertContext {
    fn drop(&mut self) {
        unsafe {
            CertFreeCertificateContext(self.0);
        }
    }
}

impl Deref for CertContext {
    type Target = PCCERT_CONTEXT;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

enum KeyHandle {
    NCrypt(NCRYPT_KEY_HANDLE),
    CryptoAPI(HCRYPTPROV, DWORD),
}

impl KeyHandle {
    fn from_cert(cert: &CertContext) -> Result<KeyHandle, Error> {
        let mut key_handle = 0;
        let mut key_spec = 0;
        let mut must_free = 0;
        unsafe {
            if CryptAcquireCertificatePrivateKey(
                **cert,
                CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG,
                std::ptr::null_mut(),
                &mut key_handle,
                &mut key_spec,
                &mut must_free,
            ) != 1
            {
                return Err(error_here!(
                    ErrorType::ExternalError,
                    GetLastError().to_string()
                ));
            }
        }
        if must_free == 0 {
            return Err(error_here!(ErrorType::ExternalError));
        }
        if key_spec == CERT_NCRYPT_KEY_SPEC {
            Ok(KeyHandle::NCrypt(key_handle as NCRYPT_KEY_HANDLE))
        } else {
            Ok(KeyHandle::CryptoAPI(key_handle as HCRYPTPROV, key_spec))
        }
    }

    fn sign(
        &self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
        do_signature: bool,
        key_type: KeyType,
    ) -> Result<Vec<u8>, Error> {
        match &self {
            KeyHandle::NCrypt(ncrypt_handle) => {
                sign_ncrypt(ncrypt_handle, data, params, do_signature, key_type)
            }
            KeyHandle::CryptoAPI(hcryptprov, key_spec) => {
                sign_cryptoapi(hcryptprov, key_spec, data, params, do_signature)
            }
        }
    }
}

impl Drop for KeyHandle {
    fn drop(&mut self) {
        match self {
            KeyHandle::NCrypt(ncrypt_handle) => unsafe {
                let _ = NCryptFreeObject(*ncrypt_handle);
            },
            KeyHandle::CryptoAPI(hcryptprov, _) => unsafe {
                let _ = CryptReleaseContext(*hcryptprov, 0);
            },
        }
    }
}

fn sign_ncrypt(
    ncrypt_handle: &NCRYPT_KEY_HANDLE,
    data: &[u8],
    params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    do_signature: bool,
    key_type: KeyType,
) -> Result<Vec<u8>, Error> {
    let mut sign_params = SignParams::new(key_type, params)?;
    let params_ptr = sign_params.params_ptr();
    let flags = sign_params.flags();
    let mut data = data.to_vec();
    let mut signature_len = 0;
    // We call NCryptSignHash twice: the first time to get the size of the buffer we need to
    // allocate and then again to actually sign the data, if `do_signature` is `true`.
    let status = unsafe {
        NCryptSignHash(
            *ncrypt_handle,
            params_ptr,
            data.as_mut_ptr(),
            data.len()
                .try_into()
                .map_err(|_| error_here!(ErrorType::ValueTooLarge))?,
            std::ptr::null_mut(),
            0,
            &mut signature_len,
            flags,
        )
    };
    // 0 is "ERROR_SUCCESS" (but "ERROR_SUCCESS" is unsigned, whereas SECURITY_STATUS is signed)
    if status != 0 {
        return Err(error_here!(ErrorType::ExternalError, status.to_string()));
    }
    let mut signature = vec![0; signature_len as usize];
    if !do_signature {
        return Ok(signature);
    }
    let mut final_signature_len = signature_len;
    let status = unsafe {
        NCryptSignHash(
            *ncrypt_handle,
            params_ptr,
            data.as_mut_ptr(),
            data.len()
                .try_into()
                .map_err(|_| error_here!(ErrorType::ValueTooLarge))?,
            signature.as_mut_ptr(),
            signature_len,
            &mut final_signature_len,
            flags,
        )
    };
    if status != 0 {
        return Err(error_here!(ErrorType::ExternalError, status.to_string()));
    }
    if final_signature_len != signature_len {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(signature)
}

fn sign_cryptoapi(
    hcryptprov: &HCRYPTPROV,
    key_spec: &DWORD,
    data: &[u8],
    params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    do_signature: bool,
) -> Result<Vec<u8>, Error> {
    if params.is_some() {
        return Err(error_here!(ErrorType::LibraryFailure));
    }
    // data will be an encoded DigestInfo, which specifies the hash algorithm and bytes of the hash
    // to sign. However, CryptoAPI requires directly specifying the bytes of the hash, so it must
    // be extracted first.
    let (_, hash_bytes) = read_digest_info(data)?;
    let hash = HCryptHash::new(hcryptprov, hash_bytes)?;
    let mut signature_len = 0;
    if unsafe {
        CryptSignHashW(
            *hash,
            *key_spec,
            std::ptr::null_mut(),
            0,
            std::ptr::null_mut(),
            &mut signature_len,
        )
    } != 1
    {
        return Err(error_here!(
            ErrorType::ExternalError,
            unsafe { GetLastError() }.to_string()
        ));
    }
    let mut signature = vec![0; signature_len as usize];
    if !do_signature {
        return Ok(signature);
    }
    let mut final_signature_len = signature_len;
    if unsafe {
        CryptSignHashW(
            *hash,
            *key_spec,
            std::ptr::null_mut(),
            0,
            signature.as_mut_ptr(),
            &mut final_signature_len,
        )
    } != 1
    {
        return Err(error_here!(
            ErrorType::ExternalError,
            unsafe { GetLastError() }.to_string()
        ));
    }
    if final_signature_len != signature_len {
        return Err(error_here!(ErrorType::ExternalError));
    }
    // CryptoAPI returns the signature with the most significant byte last (little-endian),
    // whereas PKCS#11 expects the most significant byte first (big-endian).
    signature.reverse();
    Ok(signature)
}

struct HCryptHash(HCRYPTHASH);

impl HCryptHash {
    fn new(hcryptprov: &HCRYPTPROV, hash_bytes: &[u8]) -> Result<HCryptHash, Error> {
        let alg = match hash_bytes.len() {
            20 => CALG_SHA1,
            32 => CALG_SHA_256,
            48 => CALG_SHA_384,
            64 => CALG_SHA_512,
            _ => {
                return Err(error_here!(ErrorType::UnsupportedInput));
            }
        };
        let mut hash: HCRYPTHASH = 0;
        if unsafe { CryptCreateHash(*hcryptprov, alg, 0, 0, &mut hash) } != 1 {
            return Err(error_here!(
                ErrorType::ExternalError,
                unsafe { GetLastError() }.to_string()
            ));
        }
        if unsafe { CryptSetHashParam(hash, HP_HASHVAL, hash_bytes.as_ptr(), 0) } != 1 {
            return Err(error_here!(
                ErrorType::ExternalError,
                unsafe { GetLastError() }.to_string()
            ));
        }
        Ok(HCryptHash(hash))
    }
}

impl Drop for HCryptHash {
    fn drop(&mut self) {
        unsafe {
            CryptDestroyHash(self.0);
        }
    }
}

impl Deref for HCryptHash {
    type Target = HCRYPTHASH;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

// In some cases, the ncrypt API takes a pointer to a null-terminated wide-character string as a way
// of specifying an algorithm. The "right" way to do this would be to take the corresponding
// &'static str constant provided by the winapi crate, create an OsString from it, encode it as wide
// characters, and collect it into a Vec<u16>. However, since the implementation that provides this
// functionality isn't constant, we would have to manage the memory this creates and uses. Since
// rust structures generally can't be self-referrential, this memory would have to live elsewhere,
// and the nice abstractions we've created for this implementation start to break down. It's much
// simpler to hard-code the identifiers we support, since there are only four of them.
// The following arrays represent the identifiers "SHA1", "SHA256", "SHA384", and "SHA512",
// respectively.
const SHA1_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 49, 0];
const SHA256_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 50, 53, 54, 0];
const SHA384_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 51, 56, 52, 0];
const SHA512_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 53, 49, 50, 0];

enum SignParams {
    EC,
    RSA_PKCS1(BCRYPT_PKCS1_PADDING_INFO),
    RSA_PSS(BCRYPT_PSS_PADDING_INFO),
}

impl SignParams {
    fn new(
        key_type: KeyType,
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<SignParams, Error> {
        // EC is easy, so handle that first.
        match key_type {
            KeyType::EC => return Ok(SignParams::EC),
            KeyType::RSA => {}
        }
        // If `params` is `Some`, we're doing RSA-PSS. If it is `None`, we're doing RSA-PKCS1.
        let pss_params = match params {
            Some(pss_params) => pss_params,
            None => {
                // The hash algorithm should be encoded in the data to be signed, so we don't have to
                // (and don't want to) specify a particular algorithm here.
                return Ok(SignParams::RSA_PKCS1(BCRYPT_PKCS1_PADDING_INFO {
                    pszAlgId: std::ptr::null(),
                }));
            }
        };
        let algorithm_string = match pss_params.hashAlg {
            CKM_SHA_1 => SHA1_ALGORITHM_STRING,
            CKM_SHA256 => SHA256_ALGORITHM_STRING,
            CKM_SHA384 => SHA384_ALGORITHM_STRING,
            CKM_SHA512 => SHA512_ALGORITHM_STRING,
            _ => {
                return Err(error_here!(ErrorType::UnsupportedInput));
            }
        };
        Ok(SignParams::RSA_PSS(BCRYPT_PSS_PADDING_INFO {
            pszAlgId: algorithm_string.as_ptr(),
            cbSalt: pss_params.sLen,
        }))
    }

    fn params_ptr(&mut self) -> *mut std::ffi::c_void {
        match self {
            SignParams::EC => std::ptr::null_mut(),
            SignParams::RSA_PKCS1(params) => {
                params as *mut BCRYPT_PKCS1_PADDING_INFO as *mut std::ffi::c_void
            }
            SignParams::RSA_PSS(params) => {
                params as *mut BCRYPT_PSS_PADDING_INFO as *mut std::ffi::c_void
            }
        }
    }

    fn flags(&self) -> u32 {
        match *self {
            SignParams::EC => 0,
            SignParams::RSA_PKCS1(_) => NCRYPT_PAD_PKCS1_FLAG,
            SignParams::RSA_PSS(_) => NCRYPT_PAD_PSS_FLAG,
        }
    }
}

/// A helper enum to identify a private key's type. We support EC and RSA.
#[allow(clippy::upper_case_acronyms)]
#[derive(Clone, Copy, Debug)]
pub enum KeyType {
    EC,
    RSA,
}

/// Represents a private key for which there exists a corresponding certificate.
pub struct Key {
    /// A handle on the OS mechanism that represents the certificate for this key.
    cert: CertContext,
    /// PKCS #11 object class. Will be `CKO_PRIVATE_KEY`.
    class: Vec<u8>,
    /// Whether or not this is on a token. Will be `CK_TRUE`.
    token: Vec<u8>,
    /// An identifier unique to this key. Must be the same as the ID for the certificate.
    id: Vec<u8>,
    /// Whether or not this key is "private" (can it be exported?). Will be CK_TRUE (it can't be
    /// exported).
    private: Vec<u8>,
    /// PKCS #11 key type. Will be `CKK_EC` for EC, and `CKK_RSA` for RSA.
    key_type: Vec<u8>,
    /// If this is an RSA key, this is the value of the modulus as an unsigned integer.
    modulus: Option<Vec<u8>>,
    /// If this is an EC key, this is the DER bytes of the OID identifying the curve the key is on.
    ec_params: Option<Vec<u8>>,
    /// An enum identifying this key's type.
    key_type_enum: KeyType,
    /// A handle on the OS mechanism that represents this key.
    key_handle: Option<KeyHandle>,
}

impl Key {
    fn new(cert_context: PCCERT_CONTEXT) -> Result<Key, Error> {
        let cert = unsafe { *cert_context };
        let cert_der =
            unsafe { slice::from_raw_parts(cert.pbCertEncoded, cert.cbCertEncoded as usize) };
        let id = Sha256::digest(cert_der).to_vec();
        let id = id.to_vec();
        let cert_info = unsafe { &*cert.pCertInfo };
        let mut modulus = None;
        let mut ec_params = None;
        let spki = &cert_info.SubjectPublicKeyInfo;
        let algorithm_oid = unsafe { CStr::from_ptr(spki.Algorithm.pszObjId) }
            .to_str()
            .map_err(|_| error_here!(ErrorType::ExternalError))?;
        let (key_type_enum, key_type_attribute) = if algorithm_oid == szOID_RSA_RSA {
            if spki.PublicKey.cUnusedBits != 0 {
                return Err(error_here!(ErrorType::ExternalError));
            }
            let public_key_bytes = unsafe {
                std::slice::from_raw_parts(spki.PublicKey.pbData, spki.PublicKey.cbData as usize)
            };
            let modulus_value = read_rsa_modulus(public_key_bytes)?;
            modulus = Some(modulus_value);
            (KeyType::RSA, CKK_RSA)
        } else if algorithm_oid == szOID_ECC_PUBLIC_KEY {
            let params = &spki.Algorithm.Parameters;
            ec_params = Some(
                unsafe { std::slice::from_raw_parts(params.pbData, params.cbData as usize) }
                    .to_vec(),
            );
            (KeyType::EC, CKK_EC)
        } else {
            return Err(error_here!(ErrorType::LibraryFailure));
        };
        let cert = CertContext::new(cert_context);
        Ok(Key {
            cert,
            class: serialize_uint(CKO_PRIVATE_KEY)?,
            token: serialize_uint(CK_TRUE)?,
            id,
            private: serialize_uint(CK_TRUE)?,
            key_type: serialize_uint(key_type_attribute)?,
            modulus,
            ec_params,
            key_type_enum,
            key_handle: None,
        })
    }

    fn class(&self) -> &[u8] {
        &self.class
    }

    fn token(&self) -> &[u8] {
        &self.token
    }

    fn id(&self) -> &[u8] {
        &self.id
    }

    fn private(&self) -> &[u8] {
        &self.private
    }

    fn key_type(&self) -> &[u8] {
        &self.key_type
    }

    fn modulus(&self) -> Option<&[u8]> {
        match &self.modulus {
            Some(modulus) => Some(modulus.as_slice()),
            None => None,
        }
    }

    fn ec_params(&self) -> Option<&[u8]> {
        match &self.ec_params {
            Some(ec_params) => Some(ec_params.as_slice()),
            None => None,
        }
    }

    fn sign_with_retry(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
        do_signature: bool,
    ) -> Result<Vec<u8>, Error> {
        let result = self.sign_internal(data, params, do_signature);
        if result.is_ok() {
            return result;
        }
        // Some devices appear to not work well when the key handle is held for too long or if a
        // card is inserted/removed while Firefox is running. Try refreshing the key handle.
        debug!("sign failed: refreshing key handle");
        let _ = self.key_handle.take();
        self.sign_internal(data, params, do_signature)
    }

    /// data: the data to sign
    /// do_signature: if true, actually perform the signature. Otherwise, return a `Vec<u8>` of the
    /// length the signature would be, if performed.
    fn sign_internal(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
        do_signature: bool,
    ) -> Result<Vec<u8>, Error> {
        // If this key hasn't been used for signing yet, there won't be a cached key handle. Obtain
        // and cache it if this is the case. Doing so can cause the underlying implementation to
        // show an authentication or pin prompt to the user. Caching the handle can avoid causing
        // multiple prompts to be displayed in some cases.
        if self.key_handle.is_none() {
            let _ = self.key_handle.replace(KeyHandle::from_cert(&self.cert)?);
        }
        let key = match &self.key_handle {
            Some(key) => key,
            None => return Err(error_here!(ErrorType::LibraryFailure)),
        };
        key.sign(data, params, do_signature, self.key_type_enum)
    }
}

impl CryptokiObject for Key {
    fn matches(&self, attrs: &[(CK_ATTRIBUTE_TYPE, Vec<u8>)]) -> bool {
        for (attr_type, attr_value) in attrs {
            let comparison = match *attr_type {
                CKA_CLASS => self.class(),
                CKA_TOKEN => self.token(),
                CKA_ID => self.id(),
                CKA_PRIVATE => self.private(),
                CKA_KEY_TYPE => self.key_type(),
                CKA_MODULUS => {
                    if let Some(modulus) = self.modulus() {
                        modulus
                    } else {
                        return false;
                    }
                }
                CKA_EC_PARAMS => {
                    if let Some(ec_params) = self.ec_params() {
                        ec_params
                    } else {
                        return false;
                    }
                }
                _ => return false,
            };
            if attr_value.as_slice() != comparison {
                return false;
            }
        }
        true
    }

    fn get_attribute(&self, attribute: CK_ATTRIBUTE_TYPE) -> Option<&[u8]> {
        match attribute {
            CKA_CLASS => Some(self.class()),
            CKA_TOKEN => Some(self.token()),
            CKA_ID => Some(self.id()),
            CKA_PRIVATE => Some(self.private()),
            CKA_KEY_TYPE => Some(self.key_type()),
            CKA_MODULUS => self.modulus(),
            CKA_EC_PARAMS => self.ec_params(),
            _ => None,
        }
    }
}

impl Sign for Key {
    fn get_signature_length(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<usize, Error> {
        match self.sign_with_retry(data, params, false) {
            Ok(dummy_signature_bytes) => Ok(dummy_signature_bytes.len()),
            Err(e) => Err(e),
        }
    }

    fn sign(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<Vec<u8>, Error> {
        self.sign_with_retry(data, params, true)
    }
}

struct CertStore {
    handle: HCERTSTORE,
}

impl Drop for CertStore {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                CertCloseStore(self.handle, 0);
            }
        }
    }
}

impl Deref for CertStore {
    type Target = HCERTSTORE;

    fn deref(&self) -> &Self::Target {
        &self.handle
    }
}

impl CertStore {
    fn new(handle: HCERTSTORE) -> CertStore {
        CertStore { handle }
    }
}

// Given a pointer to a CERT_CHAIN_CONTEXT, enumerates each chain in the context and each element
// in each chain to gather every CERT_CONTEXT pointed to by the CERT_CHAIN_CONTEXT.
// https://docs.microsoft.com/en-us/windows/win32/api/wincrypt/ns-wincrypt-cert_chain_context says
// that the 0th element of the 0th chain will be the end-entity certificate. This certificate (if
// present), will be the 0th element of the returned Vec.
fn gather_cert_contexts(cert_chain_context: *const CERT_CHAIN_CONTEXT) -> Vec<*const CERT_CONTEXT> {
    let mut cert_contexts = Vec::new();
    if cert_chain_context.is_null() {
        return cert_contexts;
    }
    let cert_chain_context = unsafe { &*cert_chain_context };
    let cert_chains = unsafe {
        std::slice::from_raw_parts(
            cert_chain_context.rgpChain,
            cert_chain_context.cChain as usize,
        )
    };
    for cert_chain in cert_chains {
        // First dereference the borrow.
        let cert_chain = *cert_chain;
        if cert_chain.is_null() {
            continue;
        }
        // Then dereference the pointer.
        let cert_chain = unsafe { &*cert_chain };
        let chain_elements = unsafe {
            std::slice::from_raw_parts(cert_chain.rgpElement, cert_chain.cElement as usize)
        };
        for chain_element in chain_elements {
            let chain_element = *chain_element; // dereference borrow
            if chain_element.is_null() {
                continue;
            }
            let chain_element = unsafe { &*chain_element }; // dereference pointer
            cert_contexts.push(chain_element.pCertContext);
        }
    }
    cert_contexts
}

pub struct Backend {}

impl ClientCertsBackend for Backend {
    type Cert = Cert;
    type Key = Key;

    /// Attempts to enumerate certificates with private keys exposed by the OS. Currently only looks in
    /// the "My" cert store of the current user. In the future this may look in more locations.
    fn find_objects(&self) -> Result<(Vec<Cert>, Vec<Key>), Error> {
        let mut certs = Vec::new();
        let mut keys = Vec::new();
        let location_flags = CERT_SYSTEM_STORE_CURRENT_USER
            | CERT_STORE_OPEN_EXISTING_FLAG
            | CERT_STORE_READONLY_FLAG;
        let store_name = match CString::new("My") {
            Ok(store_name) => store_name,
            Err(_) => return Err(error_here!(ErrorType::LibraryFailure)),
        };
        let store = CertStore::new(unsafe {
            CertOpenStore(
                CERT_STORE_PROV_SYSTEM_REGISTRY_A,
                0,
                0,
                location_flags,
                store_name.as_ptr() as *const winapi::ctypes::c_void,
            )
        });
        if store.is_null() {
            return Err(error_here!(ErrorType::ExternalError));
        }
        let find_params = CERT_CHAIN_FIND_ISSUER_PARA {
            cbSize: std::mem::size_of::<CERT_CHAIN_FIND_ISSUER_PARA>() as u32,
            pszUsageIdentifier: std::ptr::null(),
            dwKeySpec: 0,
            dwAcquirePrivateKeyFlags: 0,
            cIssuer: 0,
            rgIssuer: std::ptr::null_mut(),
            pfnFindCallback: None,
            pvFindArg: std::ptr::null_mut(),
            pdwIssuerChainIndex: std::ptr::null_mut(),
            pdwIssuerElementIndex: std::ptr::null_mut(),
        };
        let mut cert_chain_context: PCCERT_CHAIN_CONTEXT = std::ptr::null_mut();
        loop {
            // CertFindChainInStore finds all certificates with private keys in the store. It also
            // attempts to build a verified certificate chain to a trust anchor for each certificate.
            // We gather and hold onto these extra certificates so that gecko can use them when
            // filtering potential client certificates according to the acceptable CAs list sent by
            // servers when they request client certificates.
            cert_chain_context = unsafe {
                CertFindChainInStore(
                    *store,
                    X509_ASN_ENCODING,
                    CERT_CHAIN_FIND_BY_ISSUER_CACHE_ONLY_FLAG
                        | CERT_CHAIN_FIND_BY_ISSUER_CACHE_ONLY_URL_FLAG,
                    CERT_CHAIN_FIND_BY_ISSUER,
                    &find_params as *const CERT_CHAIN_FIND_ISSUER_PARA
                        as *const winapi::ctypes::c_void,
                    cert_chain_context,
                )
            };
            if cert_chain_context.is_null() {
                break;
            }
            let cert_contexts = gather_cert_contexts(cert_chain_context);
            // The 0th CERT_CONTEXT is the end-entity (i.e. the certificate with the private key we're
            // after).
            match cert_contexts.get(0) {
                Some(cert_context) => {
                    let key = match Key::new(*cert_context) {
                        Ok(key) => key,
                        Err(_) => continue,
                    };
                    let cert = match Cert::new(*cert_context) {
                        Ok(cert) => cert,
                        Err(_) => continue,
                    };
                    certs.push(cert);
                    keys.push(key);
                }
                None => {}
            };
            for cert_context in cert_contexts.iter().skip(1) {
                if let Ok(cert) = Cert::new(*cert_context) {
                    certs.push(cert);
                }
            }
        }
        Ok((certs, keys))
    }
}
