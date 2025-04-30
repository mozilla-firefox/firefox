/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c-fedid.github.io/FedCM.
 */


dictionary IdentityCredentialDisconnectOptions : IdentityProviderConfig  {
  required UTF8String accountHint;
};

 // https://w3c-fedid.github.io/FedCM/#browser-api-identity-credential-interface
[Exposed=Window, SecureContext,
 Pref="dom.security.credentialmanagement.identity.enabled"]
interface IdentityCredential : Credential {
 [Throws]
 static Promise<undefined> disconnect(optional IdentityCredentialDisconnectOptions options = {});
 readonly attribute USVString? token;
 [Throws, Pref="dom.security.credentialmanagement.identity.lightweight.enabled"]
 readonly attribute UTF8String origin;
 [Throws, Pref="dom.security.credentialmanagement.identity.lightweight.enabled"]
 constructor(IdentityCredentialInit init);
};

dictionary IdentityCredentialRequestOptions {
 sequence<IdentityProviderConfig> providers;
};

enum IdentityLoginTargetType { "redirect", "popup" };

[GenerateConversionToJS]
dictionary IdentityProviderConfig {
 UTF8String configURL;
 UTF8String clientId;
 UTF8String nonce;
 UTF8String loginHint;
 UTF8String domainHint;
 [Pref="dom.security.credentialmanagement.identity.lightweight.enabled"]
 UTF8String origin;
 [Pref="dom.security.credentialmanagement.identity.lightweight.enabled"]
 UTF8String loginURL;
 [Pref="dom.security.credentialmanagement.identity.lightweight.enabled"]
 IdentityLoginTargetType loginTarget;
 [Pref="dom.security.credentialmanagement.identity.lightweight.enabled"]
 UTF8String effectiveQueryURL;
 [Pref="dom.security.credentialmanagement.identity.lightweight.enabled"]
 UTF8String effectiveType;
};

// Lightweight only

dictionary IdentityCredentialUserData {
  required UTF8String name;
  required UTF8String iconURL;
  unsigned long long expiresAfter;
};

dictionary IdentityCredentialInit {
  required DOMString id;
  UTF8String token;
  sequence<UTF8String> effectiveOrigins;
  UTF8String effectiveQueryURL;
  UTF8String effectiveType;
  IdentityCredentialUserData uiHint;
};

// Heavyweight only

// https://w3c-fedid.github.io/FedCM/#dictdef-identityproviderwellknown
[GenerateInit]
dictionary IdentityProviderWellKnown {
  required sequence<UTF8String> provider_urls;
};

// https://w3c-fedid.github.io/FedCM/#dictdef-identityprovidericon
dictionary IdentityProviderIcon {
  required UTF8String url;
  unsigned long size;
};

// https://w3c-fedid.github.io/FedCM/#dictdef-identityproviderbranding
dictionary IdentityProviderBranding {
  USVString background_color;
  USVString color;
  sequence<IdentityProviderIcon> icons;
  USVString name;
};

// https://w3c-fedid.github.io/FedCM/#dictdef-identityproviderapiconfig
[GenerateInit, GenerateConversionToJS]
dictionary IdentityProviderAPIConfig {
  required UTF8String accounts_endpoint;
  // We do not want to gather consent for identity providers, so we
  // omit this requirement and its use: https://github.com/w3c-fedid/FedCM/issues/703
  // required UTF8String client_metadata_endpoint;
  required UTF8String id_assertion_endpoint;
  UTF8String disconnect_endpoint;
  IdentityProviderBranding branding;
  UTF8String account_label;
};


// https://w3c-fedid.github.io/FedCM/#dictdef-identityprovideraccount
dictionary IdentityProviderAccount {
  required USVString id;
  required USVString name;
  required USVString email;
  USVString given_name;
  USVString picture;
  sequence<USVString> approved_clients;
  sequence<UTF8String> login_hints;
  sequence<UTF8String> domain_hints;
  sequence<UTF8String> label_hints;
};

// https://w3c-fedid.github.io/FedCM/#dictdef-identityprovideraccountlist
[GenerateInit, GenerateConversionToJS]
dictionary IdentityProviderAccountList {
  sequence<IdentityProviderAccount> accounts;
};

// https://fedidcg.github.io/FedCM/#dictdef-identityproviderclientmetadata
[GenerateInit, GenerateConversionToJS]
dictionary IdentityProviderClientMetadata {
  USVString privacy_policy_url;
  USVString terms_of_service_url;
};

// https://fedidcg.github.io/FedCM/#dictdef-identityprovidertoken
[GenerateInit]
dictionary IdentityProviderToken {
  required USVString token;
};

// https://w3c-fedid.github.io/FedCM/#dictdef-disconnectedaccount
[GenerateInit]
dictionary DisconnectedAccount {
  required UTF8String account_id;
};
