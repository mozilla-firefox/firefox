/* -*- Mode: rust; rust-indent-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(non_upper_case_globals)]

use core_foundation::array::*;
use core_foundation::base::*;
use core_foundation::boolean::*;
use core_foundation::data::*;
use core_foundation::dictionary::*;
use core_foundation::error::*;
use core_foundation::number::*;
use core_foundation::string::*;
use libloading::{Library, Symbol};
use pkcs11_bindings::*;
use rsclientcerts::error::{Error, ErrorType};
use rsclientcerts::manager::{ClientCertsBackend, CryptokiObject, Sign};
use rsclientcerts::util::*;
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::convert::TryInto;
use std::os::raw::c_void;

// Normally we would generate this with a build script, but macos is
// cross-compiled on linux, and we'd have to figure out e.g. include paths,
// etc.. This is easier.
include!("bindings_macos.rs");

#[repr(C)]
pub struct __SecIdentity(c_void);
pub type SecIdentityRef = *const __SecIdentity;
declare_TCFType!(SecIdentity, SecIdentityRef);
impl_TCFType!(SecIdentity, SecIdentityRef, SecIdentityGetTypeID);

#[repr(C)]
pub struct __SecCertificate(c_void);
pub type SecCertificateRef = *const __SecCertificate;
declare_TCFType!(SecCertificate, SecCertificateRef);
impl_TCFType!(SecCertificate, SecCertificateRef, SecCertificateGetTypeID);

#[repr(C)]
pub struct __SecKey(c_void);
pub type SecKeyRef = *const __SecKey;
declare_TCFType!(SecKey, SecKeyRef);
impl_TCFType!(SecKey, SecKeyRef, SecKeyGetTypeID);

#[repr(C)]
pub struct __SecPolicy(c_void);
pub type SecPolicyRef = *const __SecPolicy;
declare_TCFType!(SecPolicy, SecPolicyRef);
impl_TCFType!(SecPolicy, SecPolicyRef, SecPolicyGetTypeID);

#[repr(C)]
pub struct __SecTrust(c_void);
pub type SecTrustRef = *const __SecTrust;
declare_TCFType!(SecTrust, SecTrustRef);
impl_TCFType!(SecTrust, SecTrustRef, SecTrustGetTypeID);

type SecCertificateCopyKeyType = unsafe extern "C" fn(SecCertificateRef) -> SecKeyRef;
type SecTrustEvaluateWithErrorType =
    unsafe extern "C" fn(trust: SecTrustRef, error: *mut CFErrorRef) -> bool;

#[derive(Ord, Eq, PartialOrd, PartialEq)]
enum SecStringConstant {
    // These are available in macOS 10.13
    SecKeyAlgorithmRSASignatureDigestPSSSHA1,
    SecKeyAlgorithmRSASignatureDigestPSSSHA256,
    SecKeyAlgorithmRSASignatureDigestPSSSHA384,
    SecKeyAlgorithmRSASignatureDigestPSSSHA512,
}

/// This implementation uses security framework functions and constants that
/// are not provided by the version of the SDK we build with. To work around
/// this, we attempt to open and dynamically load these functions and symbols
/// at runtime. Unfortunately this does mean that if a user is not on a new
/// enough version of macOS, they will not be able to use client certificates
/// from their keychain in Firefox until they upgrade.
struct SecurityFramework<'a> {
    sec_certificate_copy_key: Symbol<'a, SecCertificateCopyKeyType>,
    sec_trust_evaluate_with_error: Symbol<'a, SecTrustEvaluateWithErrorType>,
    sec_string_constants: BTreeMap<SecStringConstant, String>,
}

lazy_static! {
    static ref SECURITY_LIBRARY: Result<Library, String> = unsafe {
        Library::new("/System/Library/Frameworks/Security.framework/Security")
            .map_err(|e| e.to_string())
    };
}

impl<'a> SecurityFramework<'a> {
    fn new() -> Result<SecurityFramework<'a>, Error> {
        let library = match &*SECURITY_LIBRARY {
            Ok(library) => library,
            Err(e) => return Err(error_here!(ErrorType::ExternalError, e.clone())),
        };
        let sec_certificate_copy_key = unsafe {
            library
                .get::<SecCertificateCopyKeyType>(b"SecCertificateCopyKey\0")
                .map_err(|e| error_here!(ErrorType::ExternalError, e.to_string()))?
        };
        let sec_trust_evaluate_with_error = unsafe {
            library
                .get::<SecTrustEvaluateWithErrorType>(b"SecTrustEvaluateWithError\0")
                .map_err(|e| error_here!(ErrorType::ExternalError, e.to_string()))?
        };
        let mut sec_string_constants = BTreeMap::new();
        let strings_to_load = vec![
            (
                b"kSecKeyAlgorithmRSASignatureDigestPSSSHA1\0".as_ref(),
                SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA1,
            ),
            (
                b"kSecKeyAlgorithmRSASignatureDigestPSSSHA256\0".as_ref(),
                SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA256,
            ),
            (
                b"kSecKeyAlgorithmRSASignatureDigestPSSSHA384\0".as_ref(),
                SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA384,
            ),
            (
                b"kSecKeyAlgorithmRSASignatureDigestPSSSHA512\0".as_ref(),
                SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA512,
            ),
        ];
        for (symbol_name, sec_string_constant) in strings_to_load {
            let cfstring_symbol = unsafe {
                library
                    .get::<*const CFStringRef>(symbol_name)
                    .map_err(|e| error_here!(ErrorType::ExternalError, e.to_string()))?
            };
            let cfstring = unsafe { CFString::wrap_under_create_rule(**cfstring_symbol) };
            sec_string_constants.insert(sec_string_constant, cfstring.to_string());
        }
        Ok(SecurityFramework {
            sec_certificate_copy_key,
            sec_trust_evaluate_with_error,
            sec_string_constants,
        })
    }
}

struct SecurityFrameworkHolder<'a> {
    framework: Result<SecurityFramework<'a>, Error>,
}

impl<'a> SecurityFrameworkHolder<'a> {
    fn new() -> SecurityFrameworkHolder<'a> {
        SecurityFrameworkHolder {
            framework: SecurityFramework::new(),
        }
    }

    /// SecCertificateCopyKey is available in macOS 10.14
    fn sec_certificate_copy_key(&self, certificate: &SecCertificate) -> Result<SecKey, Error> {
        match &self.framework {
            Ok(framework) => unsafe {
                let result =
                    (framework.sec_certificate_copy_key)(certificate.as_concrete_TypeRef());
                if result.is_null() {
                    return Err(error_here!(ErrorType::ExternalError));
                }
                Ok(SecKey::wrap_under_create_rule(result))
            },
            Err(e) => Err(e.clone()),
        }
    }

    /// SecTrustEvaluateWithError is available in macOS 10.14
    fn sec_trust_evaluate_with_error(&self, trust: &SecTrust) -> Result<bool, Error> {
        match &self.framework {
            Ok(framework) => unsafe {
                Ok((framework.sec_trust_evaluate_with_error)(
                    trust.as_concrete_TypeRef(),
                    std::ptr::null_mut(),
                ))
            },
            Err(e) => Err(e.clone()),
        }
    }

    fn get_sec_string_constant(
        &self,
        sec_string_constant: SecStringConstant,
    ) -> Result<CFString, Error> {
        match &self.framework {
            Ok(framework) => match framework.sec_string_constants.get(&sec_string_constant) {
                Some(string) => Ok(CFString::new(string)),
                None => Err(error_here!(ErrorType::ExternalError)),
            },
            Err(e) => Err(e.clone()),
        }
    }
}

lazy_static! {
    static ref SECURITY_FRAMEWORK: SecurityFrameworkHolder<'static> =
        SecurityFrameworkHolder::new();
}

fn sec_key_create_signature(
    key: &SecKey,
    algorithm: SecKeyAlgorithm,
    data: &CFData,
) -> Result<CFData, Error> {
    let mut error = std::ptr::null_mut();
    let signature = unsafe {
        SecKeyCreateSignature(
            key.as_concrete_TypeRef(),
            algorithm,
            data.as_concrete_TypeRef(),
            &mut error,
        )
    };
    if signature.is_null() {
        let error = unsafe { CFError::wrap_under_create_rule(error) };
        return Err(error_here!(
            ErrorType::ExternalError,
            error.description().to_string()
        ));
    }
    Ok(unsafe { CFData::wrap_under_create_rule(signature) })
}

fn sec_key_copy_attributes<T: TCFType>(key: &SecKey) -> CFDictionary<CFString, T> {
    unsafe { CFDictionary::wrap_under_create_rule(SecKeyCopyAttributes(key.as_concrete_TypeRef())) }
}

fn sec_key_copy_external_representation(key: &SecKey) -> Result<CFData, Error> {
    let mut error = std::ptr::null_mut();
    let representation =
        unsafe { SecKeyCopyExternalRepresentation(key.as_concrete_TypeRef(), &mut error) };
    if representation.is_null() {
        let error = unsafe { CFError::wrap_under_create_rule(error) };
        return Err(error_here!(
            ErrorType::ExternalError,
            error.description().to_string()
        ));
    }
    Ok(unsafe { CFData::wrap_under_create_rule(representation) })
}

fn sec_identity_copy_certificate(identity: &SecIdentity) -> Result<SecCertificate, Error> {
    let mut certificate = std::ptr::null();
    let status =
        unsafe { SecIdentityCopyCertificate(identity.as_concrete_TypeRef(), &mut certificate) };
    if status != errSecSuccess {
        return Err(error_here!(ErrorType::ExternalError, status.to_string()));
    }
    if certificate.is_null() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(unsafe { SecCertificate::wrap_under_create_rule(certificate) })
}

fn sec_certificate_copy_subject_summary(certificate: &SecCertificate) -> Result<CFString, Error> {
    let result = unsafe { SecCertificateCopySubjectSummary(certificate.as_concrete_TypeRef()) };
    if result.is_null() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(unsafe { CFString::wrap_under_create_rule(result) })
}

fn sec_certificate_copy_data(certificate: &SecCertificate) -> Result<CFData, Error> {
    let result = unsafe { SecCertificateCopyData(certificate.as_concrete_TypeRef()) };
    if result.is_null() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(unsafe { CFData::wrap_under_create_rule(result) })
}

fn sec_identity_copy_private_key(identity: &SecIdentity) -> Result<SecKey, Error> {
    let mut key = std::ptr::null();
    let status = unsafe { SecIdentityCopyPrivateKey(identity.as_concrete_TypeRef(), &mut key) };
    if status != errSecSuccess {
        return Err(error_here!(ErrorType::ExternalError));
    }
    if key.is_null() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(unsafe { SecKey::wrap_under_create_rule(key) })
}

pub struct Cert {
    class: Vec<u8>,
    token: Vec<u8>,
    id: Vec<u8>,
    label: Vec<u8>,
    value: Vec<u8>,
    issuer: Vec<u8>,
    serial_number: Vec<u8>,
    subject: Vec<u8>,
}

impl Cert {
    fn new_from_identity(identity: &SecIdentity) -> Result<Cert, Error> {
        let certificate = sec_identity_copy_certificate(identity)?;
        Cert::new_from_certificate(&certificate)
    }

    fn new_from_certificate(certificate: &SecCertificate) -> Result<Cert, Error> {
        let label = sec_certificate_copy_subject_summary(certificate)?;
        let der = sec_certificate_copy_data(certificate)?;
        let der = der.bytes().to_vec();
        let id = Sha256::digest(&der).to_vec();
        let (serial_number, issuer, subject) = read_encoded_certificate_identifiers(&der)?;
        Ok(Cert {
            class: serialize_uint(CKO_CERTIFICATE)?,
            token: serialize_uint(CK_TRUE)?,
            id,
            label: label.to_string().into_bytes(),
            value: der,
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

#[allow(clippy::upper_case_acronyms)]
#[derive(Clone, Copy, Debug)]
pub enum KeyType {
    EC(usize),
    RSA,
}

#[allow(clippy::upper_case_acronyms)]
enum SignParams<'a> {
    EC(CFString, &'a [u8]),
    RSA(CFString, &'a [u8]),
}

impl<'a> SignParams<'a> {
    fn new(
        key_type: KeyType,
        data: &'a [u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<SignParams<'a>, Error> {
        match key_type {
            KeyType::EC(_) => SignParams::new_ec_params(data),
            KeyType::RSA => SignParams::new_rsa_params(params, data),
        }
    }

    fn new_ec_params(data: &'a [u8]) -> Result<SignParams<'a>, Error> {
        let algorithm = unsafe {
            CFString::wrap_under_get_rule(match data.len() {
                20 => kSecKeyAlgorithmECDSASignatureDigestX962SHA1,
                32 => kSecKeyAlgorithmECDSASignatureDigestX962SHA256,
                48 => kSecKeyAlgorithmECDSASignatureDigestX962SHA384,
                64 => kSecKeyAlgorithmECDSASignatureDigestX962SHA512,
                _ => {
                    return Err(error_here!(ErrorType::UnsupportedInput));
                }
            })
        };
        Ok(SignParams::EC(algorithm, data))
    }

    fn new_rsa_params(
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
        data: &'a [u8],
    ) -> Result<SignParams<'a>, Error> {
        if let Some(pss_params) = params {
            let algorithm = {
                let algorithm_id = match pss_params.hashAlg {
                    CKM_SHA_1 => SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA1,
                    CKM_SHA256 => SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA256,
                    CKM_SHA384 => SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA384,
                    CKM_SHA512 => SecStringConstant::SecKeyAlgorithmRSASignatureDigestPSSSHA512,
                    _ => {
                        return Err(error_here!(ErrorType::UnsupportedInput));
                    }
                };
                SECURITY_FRAMEWORK.get_sec_string_constant(algorithm_id)?
            };
            return Ok(SignParams::RSA(algorithm, data));
        }

        // Handle the case where `data` is a DigestInfo.
        if let Ok((digest_oid, hash)) = read_digest_info(data) {
            let algorithm = unsafe {
                CFString::wrap_under_create_rule(match digest_oid {
                    OID_BYTES_SHA_256 => kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256,
                    OID_BYTES_SHA_384 => kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384,
                    OID_BYTES_SHA_512 => kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512,
                    OID_BYTES_SHA_1 => kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1,
                    _ => return Err(error_here!(ErrorType::UnsupportedInput)),
                })
            };
            return Ok(SignParams::RSA(algorithm, hash));
        }

        // Handle the case where `data` is a TLS 1.0 MD5/SHA1 hash.
        if data.len() == 36 {
            let algorithm = unsafe {
                CFString::wrap_under_get_rule(kSecKeyAlgorithmRSASignatureDigestPKCS1v15Raw)
            };
            return Ok(SignParams::RSA(algorithm, data));
        }

        // Otherwise, `data` will be an emsa-pss-encoded digest that should be signed with raw RSA.
        Ok(SignParams::RSA(
            unsafe { CFString::wrap_under_create_rule(kSecKeyAlgorithmRSASignatureRaw) },
            data,
        ))
    }

    fn get_algorithm(&self) -> SecKeyAlgorithm {
        match self {
            SignParams::EC(algorithm, _) => algorithm.as_concrete_TypeRef(),
            SignParams::RSA(algorithm, _) => algorithm.as_concrete_TypeRef(),
        }
    }

    fn get_data_to_sign(&self) -> &'a [u8] {
        match self {
            SignParams::EC(_, data_to_sign) => data_to_sign,
            SignParams::RSA(_, data_to_sign) => data_to_sign,
        }
    }
}

pub struct Key {
    identity: SecIdentity,
    class: Vec<u8>,
    token: Vec<u8>,
    id: Vec<u8>,
    private: Vec<u8>,
    key_type: Vec<u8>,
    modulus: Option<Vec<u8>>,
    ec_params: Option<Vec<u8>>,
    key_type_enum: KeyType,
    key_handle: Option<SecKey>,
}

impl Key {
    fn new(identity: &SecIdentity) -> Result<Key, Error> {
        let certificate = sec_identity_copy_certificate(identity)?;
        let der = sec_certificate_copy_data(&certificate)?;
        let id = Sha256::digest(der.bytes()).to_vec();
        let key = SECURITY_FRAMEWORK.sec_certificate_copy_key(&certificate)?;
        let key_type: CFString = get_key_attribute(&key, unsafe { kSecAttrKeyType })?;
        let key_size_in_bits: CFNumber = get_key_attribute(&key, unsafe { kSecAttrKeySizeInBits })?;
        let mut modulus = None;
        let mut ec_params = None;
        let sec_attr_key_type_ec =
            unsafe { CFString::wrap_under_create_rule(kSecAttrKeyTypeECSECPrimeRandom) };
        let (key_type_enum, key_type_attribute) =
            if key_type.as_concrete_TypeRef() == unsafe { kSecAttrKeyTypeRSA } {
                let public_key = sec_key_copy_external_representation(&key)?;
                let modulus_value = read_rsa_modulus(public_key.bytes())?;
                modulus = Some(modulus_value);
                (KeyType::RSA, CKK_RSA)
            } else if key_type == sec_attr_key_type_ec {
                // Assume all EC keys are secp256r1, secp384r1, or secp521r1. This
                // is wrong, but the API doesn't seem to give us a way to determine
                // which curve this key is on.
                // This might not matter in practice, because it seems all NSS uses
                // this for is to get the signature size.
                let key_size_in_bits = match key_size_in_bits.to_i64() {
                    Some(value) => value,
                    None => return Err(error_here!(ErrorType::ValueTooLarge)),
                };
                match key_size_in_bits {
                    256 => ec_params = Some(ENCODED_OID_BYTES_SECP256R1.to_vec()),
                    384 => ec_params = Some(ENCODED_OID_BYTES_SECP384R1.to_vec()),
                    521 => ec_params = Some(ENCODED_OID_BYTES_SECP521R1.to_vec()),
                    _ => return Err(error_here!(ErrorType::UnsupportedInput)),
                }
                let coordinate_width = (key_size_in_bits as usize + 7) / 8;
                (KeyType::EC(coordinate_width), CKK_EC)
            } else {
                return Err(error_here!(ErrorType::LibraryFailure));
            };

        Ok(Key {
            identity: identity.clone(),
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

    fn sign_internal(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<Vec<u8>, Error> {
        // If this key hasn't been used for signing yet, there won't be a cached key handle. Obtain
        // and cache it if this is the case. Doing so can cause the underlying implementation to
        // show an authentication or pin prompt to the user. Caching the handle can avoid causing
        // multiple prompts to be displayed in some cases.
        if self.key_handle.is_none() {
            let _ = self
                .key_handle
                .replace(sec_identity_copy_private_key(&self.identity)?);
        }
        let key = match &self.key_handle {
            Some(key) => key,
            None => return Err(error_here!(ErrorType::LibraryFailure)),
        };
        let sign_params = SignParams::new(self.key_type_enum, data, params)?;
        let signing_algorithm = sign_params.get_algorithm();
        let data_to_sign = CFData::from_buffer(sign_params.get_data_to_sign());
        let signature = sec_key_create_signature(key, signing_algorithm, &data_to_sign)?;
        let signature_value = match self.key_type_enum {
            KeyType::EC(coordinate_width) => {
                // We need to convert the DER Ecdsa-Sig-Value to the
                // concatenation of r and s, the coordinates of the point on
                // the curve. r and s must be 0-padded to be coordinate_width
                // total bytes.
                der_ec_sig_to_raw(signature.bytes(), coordinate_width)?
            }
            KeyType::RSA => signature.bytes().to_vec(),
        };
        Ok(signature_value)
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
        // Unfortunately we don't have a way of getting the length of a signature without creating
        // one.
        let dummy_signature_bytes = self.sign(data, params)?;
        Ok(dummy_signature_bytes.len())
    }

    // The input data is a hash. What algorithm we use depends on the size of the hash.
    fn sign(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<Vec<u8>, Error> {
        let result = self.sign_internal(data, params);
        if result.is_ok() {
            return result;
        }
        // Some devices appear to not work well when the key handle is held for too long or if a
        // card is inserted/removed while Firefox is running. Try refreshing the key handle.
        let _ = self.key_handle.take();
        let result = self.sign_internal(data, params);
        // If this succeeded, return the result.
        if result.is_ok() {
            return result;
        }
        // If signing failed and this is an RSA-PSS signature, perhaps the token the key is on does
        // not support RSA-PSS. In that case, emsa-pss-encode the data (hash, really) and try
        // signing with raw RSA.
        let Some(params) = params.as_ref() else {
            return result;
        };
        // `params` should only be `Some` if this is an RSA key.
        let Some(modulus) = self.modulus.as_ref() else {
            return Err(error_here!(ErrorType::LibraryFailure));
        };
        let emsa_pss_encoded = emsa_pss_encode(data, modulus_bit_length(modulus) - 1, params)?;
        self.sign_internal(&emsa_pss_encoded, &None)
    }
}

fn get_key_attribute<T: TCFType + Clone>(key: &SecKey, attr: CFStringRef) -> Result<T, Error> {
    let attributes: CFDictionary<CFString, T> = sec_key_copy_attributes(key);
    match attributes.find(attr as *const _) {
        Some(value) => Ok((*value).clone()),
        None => Err(error_here!(ErrorType::ExternalError)),
    }
}

// Given a SecIdentity, attempts to build as much of a path to a trust anchor as possible, gathers
// the CA certificates from that path, and returns them. The purpose of this function is not to
// validate the given certificate but to find CA certificates that gecko may need to do path
// building when filtering client certificates according to the acceptable CA list sent by the
// server during client authentication.
fn get_issuers(identity: &SecIdentity) -> Result<Vec<SecCertificate>, Error> {
    let certificate = sec_identity_copy_certificate(identity)?;
    let policy = unsafe { SecPolicyCreateSSL(false, std::ptr::null()) };
    if policy.is_null() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    let policy = unsafe { SecPolicy::wrap_under_create_rule(policy) };
    let mut trust = std::ptr::null();
    // Each of SecTrustCreateWithCertificates' input arguments can be either single items or an
    // array of items. Since we only want to specify one of each, we directly specify the arguments.
    let status = unsafe {
        SecTrustCreateWithCertificates(
            certificate.as_concrete_TypeRef(),
            policy.as_concrete_TypeRef(),
            &mut trust,
        )
    };
    if status != errSecSuccess {
        return Err(error_here!(ErrorType::ExternalError));
    }
    if trust.is_null() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    let trust = unsafe { SecTrust::wrap_under_create_rule(trust) };
    // Disable AIA fetching so that SecTrustEvaluateWithError doesn't result in network I/O.
    let status = unsafe { SecTrustSetNetworkFetchAllowed(trust.as_concrete_TypeRef(), 0) };
    if status != errSecSuccess {
        return Err(error_here!(ErrorType::ExternalError));
    }
    // We ignore the return value here because we don't care if the certificate is trusted or not -
    // we're only doing this to build its issuer chain as much as possible.
    let _ = SECURITY_FRAMEWORK.sec_trust_evaluate_with_error(&trust)?;
    let certificate_count = unsafe { SecTrustGetCertificateCount(trust.as_concrete_TypeRef()) };
    let mut certificates = Vec::with_capacity(
        certificate_count
            .try_into()
            .map_err(|_| error_here!(ErrorType::ValueTooLarge))?,
    );
    for i in 1..certificate_count {
        let certificate = unsafe { SecTrustGetCertificateAtIndex(trust.as_concrete_TypeRef(), i) };
        if certificate.is_null() {
            error!("SecTrustGetCertificateAtIndex returned null certificate?");
            continue;
        }
        let certificate = unsafe { SecCertificate::wrap_under_get_rule(certificate) };
        certificates.push(certificate);
    }
    Ok(certificates)
}

pub struct Backend {}

impl ClientCertsBackend for Backend {
    type Cert = Cert;
    type Key = Key;

    fn find_objects(&self) -> Result<(Vec<Cert>, Vec<Key>), Error> {
        let mut certs = Vec::new();
        let mut keys = Vec::new();
        let identities = unsafe {
            let class_key = CFString::wrap_under_get_rule(kSecClass);
            let class_value = CFString::wrap_under_get_rule(kSecClassIdentity);
            let return_ref_key = CFString::wrap_under_get_rule(kSecReturnRef);
            let return_ref_value = CFBoolean::wrap_under_get_rule(kCFBooleanTrue);
            let match_key = CFString::wrap_under_get_rule(kSecMatchLimit);
            let match_value = CFString::wrap_under_get_rule(kSecMatchLimitAll);
            let vals = vec![
                (class_key.as_CFType(), class_value.as_CFType()),
                (return_ref_key.as_CFType(), return_ref_value.as_CFType()),
                (match_key.as_CFType(), match_value.as_CFType()),
            ];
            let dict = CFDictionary::from_CFType_pairs(&vals);
            let mut result = std::ptr::null();
            let status = SecItemCopyMatching(dict.as_CFTypeRef() as CFDictionaryRef, &mut result);
            if status == errSecItemNotFound {
                return Ok((certs, keys));
            }
            if status != errSecSuccess {
                return Err(error_here!(ErrorType::ExternalError, status.to_string()));
            }
            if result.is_null() {
                return Err(error_here!(ErrorType::ExternalError));
            }
            CFArray::<SecIdentityRef>::wrap_under_create_rule(result as CFArrayRef)
        };
        for identity in identities.get_all_values().iter() {
            let identity = unsafe { SecIdentity::wrap_under_get_rule(*identity as SecIdentityRef) };
            let cert = Cert::new_from_identity(&identity);
            let key = Key::new(&identity);
            if let (Ok(cert), Ok(key)) = (cert, key) {
                certs.push(cert);
                keys.push(key);
            } else {
                continue;
            }
            if let Ok(issuers) = get_issuers(&identity) {
                for issuer in issuers {
                    if let Ok(cert) = Cert::new_from_certificate(&issuer) {
                        certs.push(cert);
                    }
                }
            }
        }
        Ok((certs, keys))
    }
}
