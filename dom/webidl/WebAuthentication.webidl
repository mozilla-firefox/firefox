/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/webauthn/
 */

/***** Interfaces to Data *****/

[SecureContext, Pref="security.webauth.webauthn",
 Exposed=Window]
interface PublicKeyCredential : Credential {
    [SameObject, Throws] readonly attribute ArrayBuffer      rawId;
    [SameObject] readonly attribute AuthenticatorResponse    response;
    readonly attribute DOMString?                            authenticatorAttachment;
    AuthenticationExtensionsClientOutputs getClientExtensionResults();
    [NewObject] static Promise<boolean> isConditionalMediationAvailable();
    [Throws, Pref="security.webauthn.enable_json_serialization_methods"] object toJSON();
};

typedef DOMString Base64URLString;

[GenerateConversionToJS]
dictionary RegistrationResponseJSON {
    required Base64URLString id;
    required Base64URLString rawId;
    required AuthenticatorAttestationResponseJSON response;
    DOMString authenticatorAttachment;
    required AuthenticationExtensionsClientOutputsJSON clientExtensionResults;
    required DOMString type;
};

[GenerateConversionToJS]
dictionary AuthenticatorAttestationResponseJSON {
    required Base64URLString clientDataJSON;
    required Base64URLString authenticatorData;
    required sequence<DOMString> transports;
    // The publicKey field will be missing if pubKeyCredParams was used to
    // negotiate a public-key algorithm that the user agent doesn’t
    // understand. (See section “Easily accessing credential data” for a
    // list of which algorithms user agents must support.) If using such an
    // algorithm then the public key must be parsed directly from
    // attestationObject or authenticatorData.
    Base64URLString publicKey;
    required long long publicKeyAlgorithm;
    // This value contains copies of some of the fields above. See
    // section “Easily accessing credential data”.
    required Base64URLString attestationObject;
};

[GenerateConversionToJS]
dictionary AuthenticationResponseJSON {
    required Base64URLString id;
    required Base64URLString rawId;
    required AuthenticatorAssertionResponseJSON response;
    DOMString authenticatorAttachment;
    required AuthenticationExtensionsClientOutputsJSON clientExtensionResults;
    required DOMString type;
};

[GenerateConversionToJS]
dictionary AuthenticatorAssertionResponseJSON {
    required Base64URLString clientDataJSON;
    required Base64URLString authenticatorData;
    required Base64URLString signature;
    Base64URLString userHandle;
    Base64URLString attestationObject;
};

[GenerateConversionToJS]
dictionary AuthenticationExtensionsClientOutputsJSON {
};

[SecureContext]
partial interface PublicKeyCredential {
    [NewObject] static Promise<boolean> isUserVerifyingPlatformAuthenticatorAvailable();
};

[SecureContext]
partial interface PublicKeyCredential {
    [Throws, Pref="security.webauthn.enable_json_serialization_methods"] static PublicKeyCredentialCreationOptions parseCreationOptionsFromJSON(PublicKeyCredentialCreationOptionsJSON options);
};

// https://w3c.github.io/webauthn/#sctn-getClientCapabilities
[SecureContext]
partial interface PublicKeyCredential {
        [Throws] static Promise<PublicKeyCredentialClientCapabilities> getClientCapabilities();
};

typedef record<DOMString, boolean> PublicKeyCredentialClientCapabilities;

dictionary PublicKeyCredentialCreationOptionsJSON {
    required PublicKeyCredentialRpEntity                    rp;
    required PublicKeyCredentialUserEntityJSON              user;
    required Base64URLString                                challenge;
    required sequence<PublicKeyCredentialParameters>        pubKeyCredParams;
    unsigned long                                           timeout;
    sequence<PublicKeyCredentialDescriptorJSON>             excludeCredentials = [];
    AuthenticatorSelectionCriteria                          authenticatorSelection;
    sequence<DOMString>                                     hints = [];
    DOMString                                               attestation = "none";
    sequence<DOMString>                                     attestationFormats = [];
    AuthenticationExtensionsClientInputsJSON                extensions;
};

dictionary PublicKeyCredentialUserEntityJSON {
    required Base64URLString        id;
    required DOMString              name;
    required DOMString              displayName;
};

dictionary PublicKeyCredentialDescriptorJSON {
    required Base64URLString        id;
    required DOMString              type;
    sequence<DOMString>             transports;
};

dictionary AuthenticationExtensionsClientInputsJSON {
};

[SecureContext]
partial interface PublicKeyCredential {
    [Throws, Pref="security.webauthn.enable_json_serialization_methods"] static PublicKeyCredentialRequestOptions parseRequestOptionsFromJSON(PublicKeyCredentialRequestOptionsJSON options);
};

dictionary PublicKeyCredentialRequestOptionsJSON {
    required Base64URLString                                challenge;
    unsigned long                                           timeout;
    DOMString                                               rpId;
    sequence<PublicKeyCredentialDescriptorJSON>             allowCredentials = [];
    DOMString                                               userVerification = "preferred";
    sequence<DOMString>                                     hints = [];
    DOMString                                               attestation = "none";
    sequence<DOMString>                                     attestationFormats = [];
    AuthenticationExtensionsClientInputsJSON                extensions;
};

[SecureContext, Pref="security.webauth.webauthn",
 Exposed=Window]
interface AuthenticatorResponse {
    [SameObject, Throws] readonly attribute ArrayBuffer clientDataJSON;
};

[SecureContext, Pref="security.webauth.webauthn",
 Exposed=Window]
interface AuthenticatorAttestationResponse : AuthenticatorResponse {
    [SameObject, Throws] readonly attribute ArrayBuffer attestationObject;
    sequence<DOMString>                                 getTransports();
    [Throws] ArrayBuffer                                getAuthenticatorData();
    [Throws] ArrayBuffer?                               getPublicKey();
    [Throws] COSEAlgorithmIdentifier                    getPublicKeyAlgorithm();
};

[SecureContext, Pref="security.webauth.webauthn",
 Exposed=Window]
interface AuthenticatorAssertionResponse : AuthenticatorResponse {
    [SameObject, Throws] readonly attribute ArrayBuffer      authenticatorData;
    [SameObject, Throws] readonly attribute ArrayBuffer      signature;
    [SameObject, Throws] readonly attribute ArrayBuffer?     userHandle;
};

dictionary PublicKeyCredentialParameters {
    required DOMString                type;
    required COSEAlgorithmIdentifier  alg;
};

dictionary PublicKeyCredentialCreationOptions {
    required PublicKeyCredentialRpEntity   rp;
    required PublicKeyCredentialUserEntity user;

    required BufferSource                            challenge;
    required sequence<PublicKeyCredentialParameters> pubKeyCredParams;

    unsigned long                                timeout;
    sequence<PublicKeyCredentialDescriptor>      excludeCredentials = [];
    // FIXME: bug 1493860: should this "= {}" be here?
    AuthenticatorSelectionCriteria               authenticatorSelection = {};
    DOMString                                    attestation = "none";
    // FIXME: bug 1493860: should this "= {}" be here?
    AuthenticationExtensionsClientInputs         extensions = {};
};

dictionary PublicKeyCredentialEntity {
    required DOMString    name;
};

dictionary PublicKeyCredentialRpEntity : PublicKeyCredentialEntity {
    DOMString      id;
};

dictionary PublicKeyCredentialUserEntity : PublicKeyCredentialEntity {
    required BufferSource   id;
    required DOMString      displayName;
};

dictionary AuthenticatorSelectionCriteria {
    DOMString                    authenticatorAttachment;
    DOMString                    residentKey;
    boolean                      requireResidentKey = false;
    DOMString                    userVerification = "preferred";
};

dictionary PublicKeyCredentialRequestOptions {
    required BufferSource                challenge;
    unsigned long                        timeout;
    USVString                            rpId;
    sequence<PublicKeyCredentialDescriptor> allowCredentials = [];
    DOMString                            userVerification = "preferred";
    // FIXME: bug 1493860: should this "= {}" be here?
    AuthenticationExtensionsClientInputs extensions = {};
};

dictionary AuthenticationExtensionsClientInputs {
};

dictionary AuthenticationExtensionsClientOutputs {
};

typedef record<DOMString, DOMString> AuthenticationExtensionsAuthenticatorInputs;

// The CollectedClientData dictionary must be serialized using the algorithm
// from https://w3c.github.io/webauthn/#clientdatajson-serialization. Because
// CollectedClientData is only consumed by the relying party, and because
// [GenerateToJSON] does not produce the correct serialization algorithm, the
// definition below is commented out. Please keep this definition in sync with
// in AssembleClientData in dom/webauthn/WebAuthnTransactionParent.cpp.
//
// dictionary CollectedClientData {
//     required DOMString           type;
//     required DOMString           challenge;
//     required DOMString           origin;
//     boolean                      crossOrigin;
//     DOMString                    topOrigin;
// };

dictionary PublicKeyCredentialDescriptor {
    required DOMString                    type;
    required BufferSource                 id;
    // Transports is a string that is matched against the AuthenticatorTransport
    // enumeration so that we have forward-compatibility for new transports.
    sequence<DOMString>                   transports;
};

typedef long COSEAlgorithmIdentifier;

typedef sequence<AAGUID>      AuthenticatorSelectionList;

typedef BufferSource      AAGUID;

partial dictionary AuthenticationExtensionsClientInputs {
    USVString appid;
};

partial dictionary AuthenticationExtensionsClientOutputs {
    boolean appid;
};

// The spec does not define any partial dictionaries that modify
// AuthenticationExtensionsClientInputsJSON, but this seems to be an error. All changes to
// AuthenticationExtensionsClientInputs must be accompanied by changes to
// AuthenticationExtensionsClientInputsJSON for parseCreationOptionsFromJSON and
// parseRequestOptionsFromJSON to function correctly.
// (see: https://github.com/w3c/webauthn/issues/1968).
partial dictionary AuthenticationExtensionsClientInputsJSON {
    USVString appid;
};

// We also deviate from the spec by mirroring changes to AuthenticationExtensionsClientOutputs in
// AuthenticationExtensionsClientOutputsJSON.
partial dictionary AuthenticationExtensionsClientOutputsJSON {
    boolean appid;
};

partial dictionary AuthenticationExtensionsClientInputs {
    boolean credProps;
};

partial dictionary AuthenticationExtensionsClientInputsJSON {
    boolean credProps;
};

dictionary CredentialPropertiesOutput {
    boolean rk;
};

partial dictionary AuthenticationExtensionsClientOutputs {
    CredentialPropertiesOutput credProps;
};

partial dictionary AuthenticationExtensionsClientOutputsJSON {
    CredentialPropertiesOutput credProps;
};

/*
 * CTAP2 Extensions
 * <https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-errata-20220621.html#sctn-defined-extensions>
 */

// credProtect
// <https://fidoalliance.org/specs/fido-v2.2-rd-20230321/fido-client-to-authenticator-protocol-v2.2-rd-20230321.html#sctn-credProtect-extension>
enum CredentialProtectionPolicy {
  "userVerificationOptional",
  "userVerificationOptionalWithCredentialIDList",
  "userVerificationRequired",
};

partial dictionary AuthenticationExtensionsClientInputs {
  CredentialProtectionPolicy credentialProtectionPolicy;
  // The specification includes a default `= false` value for
  // enforceCredentialProtectionPolicy. We omit it here to distinguish between
  // three logical cases: the extension was not sent, the extension was sent
  // with value false, the extension was sent with value true.
  boolean enforceCredentialProtectionPolicy;
};

partial dictionary AuthenticationExtensionsClientInputsJSON {
  CredentialProtectionPolicy credentialProtectionPolicy;
  boolean enforceCredentialProtectionPolicy;
};

// hmac-secret
// <https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-errata-20220621.html#sctn-hmac-secret-extension>
// note: we don't support hmac-secret in get() (see instead the prf extension)
// so we only define the create() inputs and outputs here.

partial dictionary AuthenticationExtensionsClientInputs {
    boolean hmacCreateSecret;
};

partial dictionary AuthenticationExtensionsClientOutputs {
    boolean hmacCreateSecret;
};

partial dictionary AuthenticationExtensionsClientInputsJSON {
    boolean hmacCreateSecret;
};

partial dictionary AuthenticationExtensionsClientOutputsJSON {
    boolean hmacCreateSecret;
};

// largeBlob
// <https://w3c.github.io/webauthn/#sctn-large-blob-extension>
partial dictionary AuthenticationExtensionsClientInputs {
    AuthenticationExtensionsLargeBlobInputs largeBlob;
};

partial dictionary AuthenticationExtensionsClientInputsJSON {
    AuthenticationExtensionsLargeBlobInputsJSON largeBlob;
};

dictionary AuthenticationExtensionsLargeBlobInputs {
    DOMString support;
    boolean read;
    BufferSource write;
};

dictionary AuthenticationExtensionsLargeBlobInputsJSON {
    DOMString support;
    boolean read;
    Base64URLString write;
};

partial dictionary AuthenticationExtensionsClientOutputs {
    AuthenticationExtensionsLargeBlobOutputs largeBlob;
};

partial dictionary AuthenticationExtensionsClientOutputsJSON {
    AuthenticationExtensionsLargeBlobOutputsJSON largeBlob;
};

dictionary AuthenticationExtensionsLargeBlobOutputs {
    boolean supported;
    ArrayBuffer blob;
    boolean written;
};

dictionary AuthenticationExtensionsLargeBlobOutputsJSON {
    boolean supported;
    Base64URLString blob;
    boolean written;
};

// minPinLength
// <https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-errata-20220621.html#sctn-minpinlength-extension>
partial dictionary AuthenticationExtensionsClientInputs {
  boolean minPinLength;
};

partial dictionary AuthenticationExtensionsClientInputsJSON {
  boolean minPinLength;
};


// prf
// <https://w3c.github.io/webauthn/#prf-extension>
dictionary AuthenticationExtensionsPRFValues {
  required BufferSource first;
  BufferSource second;
};

dictionary AuthenticationExtensionsPRFValuesJSON {
  required Base64URLString first;
  Base64URLString second;
};

dictionary AuthenticationExtensionsPRFInputs {
  AuthenticationExtensionsPRFValues eval;
  record<USVString, AuthenticationExtensionsPRFValues> evalByCredential;
};

dictionary AuthenticationExtensionsPRFInputsJSON {
  AuthenticationExtensionsPRFValuesJSON eval;
  record<USVString, AuthenticationExtensionsPRFValuesJSON> evalByCredential;
};

partial dictionary AuthenticationExtensionsClientInputs {
  AuthenticationExtensionsPRFInputs prf;
};

partial dictionary AuthenticationExtensionsClientInputsJSON {
  AuthenticationExtensionsPRFInputsJSON prf;
};

dictionary AuthenticationExtensionsPRFOutputs {
  boolean enabled;
  AuthenticationExtensionsPRFValues results;
};

dictionary AuthenticationExtensionsPRFOutputsJSON {
  boolean enabled;
  AuthenticationExtensionsPRFValuesJSON results;
};

partial dictionary AuthenticationExtensionsClientOutputs {
  AuthenticationExtensionsPRFOutputs prf;
};

partial dictionary AuthenticationExtensionsClientOutputsJSON {
  AuthenticationExtensionsPRFOutputsJSON prf;
};
