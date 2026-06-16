/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <AuthenticationServices/AuthenticationServices.h>

#include "MacOSWebAuthnService.h"

#include "CFTypeRefPtr.h"
#include "WebAuthnAutoFillEntry.h"
#include "WebAuthnEnumStrings.h"
#include "WebAuthnResult.h"
#include "WebAuthnTransportIdentifiers.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "nsCocoaUtils.h"
#include "nsIWebAuthnPromise.h"
#include "nsThreadUtils.h"

// The documentation for the platform APIs used here can be found at:
// https://developer.apple.com/documentation/authenticationservices/public-private_key_authentication/supporting_passkeys

namespace {
static mozilla::LazyLogModule gMacOSWebAuthnServiceLog("macoswebauthnservice");
}  // namespace

namespace mozilla::dom {
class API_AVAILABLE(macos(13.3)) MacOSWebAuthnService;
}  // namespace mozilla::dom

// The following ASC* declarations are from the private framework
// AuthenticationServicesCore. The full definitions can be found in WebKit's
// source at Source/WebKit/Platform/spi/Cocoa/AuthenticationServicesCoreSPI.h.
// Overriding ASAuthorizationController's _requestContextWithRequests is
// currently the only way to provide the right information to the macOS
// WebAuthn API (namely, the clientDataHash for requests made to physical
// tokens).

NS_ASSUME_NONNULL_BEGIN

@class ASCPublicKeyCredentialDescriptor;
@interface ASCPublicKeyCredentialDescriptor : NSObject <NSSecureCoding>
- (instancetype)initWithCredentialID:(NSData*)credentialID
                          transports:
                              (nullable NSArray<NSString*>*)allowedTransports;
@end

@protocol ASCPublicKeyCredentialCreationOptions
@property(nonatomic, copy) NSData* clientDataHash;
@property(nonatomic, nullable, copy) NSData* challenge;
@property(nonatomic, copy)
    NSArray<ASCPublicKeyCredentialDescriptor*>* excludedCredentials;
@end

@protocol ASCPublicKeyCredentialAssertionOptions <NSCopying>
@property(nonatomic, copy) NSData* clientDataHash;
@end

@protocol ASCCredentialRequestContext
@property(nonatomic, nullable, copy) id<ASCPublicKeyCredentialCreationOptions>
    platformKeyCredentialCreationOptions;
@property(nonatomic, nullable, copy) id<ASCPublicKeyCredentialCreationOptions>
    securityKeyCredentialCreationOptions;
@property(nonatomic, nullable, copy) id<ASCPublicKeyCredentialAssertionOptions>
    platformKeyCredentialAssertionOptions;
@property(nonatomic, nullable, copy) id<ASCPublicKeyCredentialAssertionOptions>
    securityKeyCredentialAssertionOptions;
@end

@interface ASAuthorizationController (Secrets)
- (id<ASCCredentialRequestContext>)
    _requestContextWithRequests:(NSArray<ASAuthorizationRequest*>*)requests
                          error:(NSError**)outError;
@end

NSArray<NSString*>* TransportsByteToTransportsArray(const uint8_t aTransports)
    API_AVAILABLE(macos(13.3)) {
  NSMutableArray<NSString*>* transportsNS = [[NSMutableArray alloc] init];
  if ((aTransports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_USB) ==
      MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_USB) {
    [transportsNS
        addObject:
            ASAuthorizationSecurityKeyPublicKeyCredentialDescriptorTransportUSB];
  }
  if ((aTransports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_NFC) ==
      MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_NFC) {
    [transportsNS
        addObject:
            ASAuthorizationSecurityKeyPublicKeyCredentialDescriptorTransportNFC];
  }
  if ((aTransports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_BLE) ==
      MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_BLE) {
    [transportsNS
        addObject:
            ASAuthorizationSecurityKeyPublicKeyCredentialDescriptorTransportBluetooth];
  }
  // TODO (bug 1859367): the platform doesn't have a definition for "internal"
  // transport. When it does, this code should be updated to handle it.
  return transportsNS;
}

NSArray* CredentialListsToCredentialDescriptorArray(
    const nsTArray<nsTArray<uint8_t>>& aCredentialList,
    const nsTArray<uint8_t>& aCredentialListTransports,
    const Class credentialDescriptorClass) API_AVAILABLE(macos(13.3)) {
  MOZ_ASSERT(aCredentialList.Length() == aCredentialListTransports.Length());
  NSMutableArray* credentials = [[NSMutableArray alloc] init];
  for (size_t i = 0; i < aCredentialList.Length(); i++) {
    const nsTArray<uint8_t>& credentialId = aCredentialList[i];
    const uint8_t& credentialTransports = aCredentialListTransports[i];
    NSData* credentialIdNS = [NSData dataWithBytes:credentialId.Elements()
                                            length:credentialId.Length()];
    NSArray<NSString*>* credentialTransportsNS =
        TransportsByteToTransportsArray(credentialTransports);
    NSObject* credential = [[credentialDescriptorClass alloc]
        initWithCredentialID:credentialIdNS
                  transports:credentialTransportsNS];
    [credentials addObject:credential];
  }
  return credentials;
}

// MacOSAuthorizationController is an ASAuthorizationController that overrides
// _requestContextWithRequests so that the implementation can set some options
// that aren't directly settable using the public API.
API_AVAILABLE(macos(13.3))
@interface MacOSAuthorizationController : ASAuthorizationController
@end

@implementation MacOSAuthorizationController {
  nsTArray<uint8_t> mClientDataHash;
  nsTArray<nsTArray<uint8_t>> mCredentialList;
  nsTArray<uint8_t> mCredentialListTransports;
}

- (void)setRegistrationOptions:
    (id<ASCPublicKeyCredentialCreationOptions>)registrationOptions {
  registrationOptions.clientDataHash =
      [NSData dataWithBytes:mClientDataHash.Elements()
                     length:mClientDataHash.Length()];
  // Unset challenge so that the implementation uses clientDataHash (the API
  // returns an error otherwise).
  registrationOptions.challenge = nil;
  const Class publicKeyCredentialDescriptorClass =
      NSClassFromString(@"ASCPublicKeyCredentialDescriptor");
  NSArray<ASCPublicKeyCredentialDescriptor*>* excludedCredentials =
      CredentialListsToCredentialDescriptorArray(
          mCredentialList, mCredentialListTransports,
          publicKeyCredentialDescriptorClass);
  if ([excludedCredentials count] > 0) {
    registrationOptions.excludedCredentials = excludedCredentials;
  }
}

- (void)stashClientDataHash:(nsTArray<uint8_t>&&)clientDataHash
              andCredentialList:(nsTArray<nsTArray<uint8_t>>&&)credentialList
    andCredentialListTransports:(nsTArray<uint8_t>&&)credentialListTransports {
  mClientDataHash = std::move(clientDataHash);
  mCredentialList = std::move(credentialList);
  mCredentialListTransports = std::move(credentialListTransports);
}

- (id<ASCCredentialRequestContext>)
    _requestContextWithRequests:(NSArray<ASAuthorizationRequest*>*)requests
                          error:(NSError**)outError {
  id<ASCCredentialRequestContext> context =
      [super _requestContextWithRequests:requests error:outError];

  if (context.platformKeyCredentialCreationOptions) {
    [self setRegistrationOptions:context.platformKeyCredentialCreationOptions];
  }
  if (context.securityKeyCredentialCreationOptions) {
    [self setRegistrationOptions:context.securityKeyCredentialCreationOptions];
  }

  if (context.platformKeyCredentialAssertionOptions) {
    id<ASCPublicKeyCredentialAssertionOptions> assertionOptions =
        context.platformKeyCredentialAssertionOptions;
    assertionOptions.clientDataHash =
        [NSData dataWithBytes:mClientDataHash.Elements()
                       length:mClientDataHash.Length()];
    context.platformKeyCredentialAssertionOptions =
        [assertionOptions copyWithZone:nil];
  }
  if (context.securityKeyCredentialAssertionOptions) {
    id<ASCPublicKeyCredentialAssertionOptions> assertionOptions =
        context.securityKeyCredentialAssertionOptions;
    assertionOptions.clientDataHash =
        [NSData dataWithBytes:mClientDataHash.Elements()
                       length:mClientDataHash.Length()];
    context.securityKeyCredentialAssertionOptions =
        [assertionOptions copyWithZone:nil];
  }

  return context;
}
@end

// MacOSAuthenticatorRequestDelegate is an ASAuthorizationControllerDelegate,
// which can be set as an ASAuthorizationController's delegate to be called
// back when a request for a platform authenticator attestation or assertion
// response completes either with an attestation or assertion
// (didCompleteWithAuthorization) or with an error (didCompleteWithError).
API_AVAILABLE(macos(13.3))
@interface MacOSAuthenticatorRequestDelegate
    : NSObject <ASAuthorizationControllerDelegate>
- (void)setCallback:(mozilla::dom::MacOSWebAuthnService*)callback
      transactionId:(uint64_t)transactionId;
@end

// MacOSAuthenticatorPresentationContextProvider is an
// ASAuthorizationControllerPresentationContextProviding, which can be set as
// an ASAuthorizationController's presentationContextProvider, and provides a
// presentation anchor for the ASAuthorizationController. Basically, this
// provides the API a handle to the window that made the request.
API_AVAILABLE(macos(13.3))
@interface MacOSAuthenticatorPresentationContextProvider
    : NSObject <ASAuthorizationControllerPresentationContextProviding>
@property(nonatomic, strong) NSWindow* window;
@end

namespace mozilla::dom {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
class API_AVAILABLE(macos(13.3)) MacOSWebAuthnService final
    : public nsIWebAuthnService {
 public:
  MacOSWebAuthnService() = default;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWEBAUTHNSERVICE

  void FinishMakeCredential(uint64_t aTransactionId,
                            nsTArray<uint8_t>&& aRawAttestationObject,
                            nsTArray<uint8_t>&& aCredentialId,
                            nsTArray<nsString>&& aTransports,
                            Maybe<nsString>&& aAuthenticatorAttachment,
                            Maybe<bool> aLargeBlobSupported,
                            Maybe<bool> aPrfSupported,
                            Maybe<nsTArray<uint8_t>>&& aPrfFirst,
                            Maybe<nsTArray<uint8_t>>&& aPrfSecond);

  void FinishGetAssertion(
      uint64_t aTransactionId, nsTArray<uint8_t>&& aCredentialId,
      nsTArray<uint8_t>&& aSignature, nsTArray<uint8_t>&& aAuthenticatorData,
      nsTArray<uint8_t>&& aUserHandle,
      Maybe<nsString>&& aAuthenticatorAttachment, Maybe<bool> aUsedAppId,
      Maybe<nsTArray<uint8_t>>&& aLargeBlobValue, Maybe<bool> aLargeBlobWritten,
      Maybe<nsTArray<uint8_t>>&& aPrfFirst,
      Maybe<nsTArray<uint8_t>>&& aPrfSecond);
  void ReleasePlatformResources();
  // Internal cleanup helper — does not check tid. Used when superseding stale
  // state during MakeCredential/GetAssertion.
  void AbortTransaction(nsresult aError);
  // Delegate-facing overload that checks the tid matches the active
  // transaction before doing anything. Releases platform resources on
  // completion.
  void AbortTransaction(uint64_t aTransactionId, nsresult aError);

 private:
  ~MacOSWebAuthnService() = default;

  void PerformRequests(uint64_t aTransactionId,
                       NSArray<ASAuthorizationRequest*>* aRequests,
                       nsTArray<uint8_t>&& aClientDataHash,
                       nsTArray<nsTArray<uint8_t>>&& aCredentialList,
                       nsTArray<uint8_t>&& aCredentialListTransports,
                       uint64_t aBrowsingContextId);

  // Main thread only:
  Maybe<uint64_t> mTransactionId;
  ASAuthorizationWebBrowserPublicKeyCredentialManager* mCredentialManager = nil;
  nsCOMPtr<nsIWebAuthnRegisterPromise> mRegisterPromise;
  nsCOMPtr<nsIWebAuthnSignPromise> mSignPromise;
  MacOSAuthorizationController* mAuthorizationController = nil;
  MacOSAuthenticatorRequestDelegate* mRequestDelegate = nil;
  MacOSAuthenticatorPresentationContextProvider* mPresentationContextProvider =
      nil;
};
#pragma clang diagnostic pop

}  // namespace mozilla::dom

nsTArray<uint8_t> NSDataToArray(NSData* data) {
  nsTArray<uint8_t> array(reinterpret_cast<const uint8_t*>(data.bytes),
                          data.length);
  return array;
}

API_AVAILABLE(macos(15.0))
NSDictionary<NSData*, ASAuthorizationPublicKeyCredentialPRFAssertionInputValues*>* _Nullable ConstructPrfEvalByCredentialEntries(
    const RefPtr<nsIWebAuthnSignArgs>& aArgs) {
  nsTArray<nsTArray<uint8_t>> prfEvalByCredIds;
  nsTArray<nsTArray<uint8_t>> prfEvalByCredFirsts;
  nsTArray<bool> prfEvalByCredSecondMaybes;
  nsTArray<nsTArray<uint8_t>> prfEvalByCredSeconds;
  if (NS_FAILED(aArgs->GetPrfEvalByCredentialCredentialId(prfEvalByCredIds)) ||
      NS_FAILED(aArgs->GetPrfEvalByCredentialEvalFirst(prfEvalByCredFirsts)) ||
      NS_FAILED(aArgs->GetPrfEvalByCredentialEvalSecondMaybe(
          prfEvalByCredSecondMaybes)) ||
      NS_FAILED(
          aArgs->GetPrfEvalByCredentialEvalSecond(prfEvalByCredSeconds)) ||
      prfEvalByCredIds.Length() != prfEvalByCredFirsts.Length() ||
      prfEvalByCredIds.Length() != prfEvalByCredSecondMaybes.Length() ||
      prfEvalByCredIds.Length() != prfEvalByCredSeconds.Length()) {
    return nil;
  }

  uint32_t count = prfEvalByCredIds.Length();
  NSMutableArray<NSData*>* keys = [NSMutableArray arrayWithCapacity:count];
  NSMutableArray<ASAuthorizationPublicKeyCredentialPRFAssertionInputValues*>*
      objects = [NSMutableArray arrayWithCapacity:count];
  for (size_t i = 0; i < count; i++) {
    NSData* saltInput1 = [NSData dataWithBytes:prfEvalByCredFirsts[i].Elements()
                                        length:prfEvalByCredFirsts[i].Length()];
    NSData* saltInput2 = nil;
    if (prfEvalByCredSecondMaybes[i]) {
      saltInput2 = [NSData dataWithBytes:prfEvalByCredSeconds[i].Elements()
                                  length:prfEvalByCredSeconds[i].Length()];
    }
    [keys addObject:[NSData dataWithBytes:prfEvalByCredIds[i].Elements()
                                   length:prfEvalByCredIds[i].Length()]];
    [objects
        addObject:[[ASAuthorizationPublicKeyCredentialPRFAssertionInputValues
                      alloc] initWithSaltInput1:saltInput1
                                     saltInput2:saltInput2]];
  }

  return [NSDictionary dictionaryWithObjects:objects forKeys:keys];
}

@implementation MacOSAuthenticatorRequestDelegate {
  RefPtr<mozilla::dom::MacOSWebAuthnService> mCallback;
  uint64_t mTransactionId;
}

- (void)setCallback:(mozilla::dom::MacOSWebAuthnService*)callback
      transactionId:(uint64_t)transactionId {
  mCallback = callback;
  mTransactionId = transactionId;
}

- (void)authorizationController:(ASAuthorizationController*)controller
    didCompleteWithAuthorization:(ASAuthorization*)authorization {
  if ([authorization.credential
          conformsToProtocol:
              @protocol(ASAuthorizationPublicKeyCredentialRegistration)]) {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("MacOSAuthenticatorRequestDelegate::didCompleteWithAuthorization: "
             "got registration"));
    id<ASAuthorizationPublicKeyCredentialRegistration> credential =
        (id<ASAuthorizationPublicKeyCredentialRegistration>)
            authorization.credential;
    nsTArray<uint8_t> rawAttestationObject(
        NSDataToArray(credential.rawAttestationObject));
    nsTArray<uint8_t> credentialId(NSDataToArray(credential.credentialID));
    nsTArray<nsString> transports;
    mozilla::Maybe<nsString> authenticatorAttachment;
    mozilla::Maybe<bool> largeBlobSupported;
    mozilla::Maybe<bool> prfSupported;
    mozilla::Maybe<nsTArray<uint8_t>> prfFirst;
    mozilla::Maybe<nsTArray<uint8_t>> prfSecond;
    if ([credential isKindOfClass:
                        [ASAuthorizationPlatformPublicKeyCredentialRegistration
                            class]]) {
      transports.AppendElement(u"hybrid"_ns);
      transports.AppendElement(u"internal"_ns);
      ASAuthorizationPlatformPublicKeyCredentialRegistration*
          platformCredential =
              (ASAuthorizationPlatformPublicKeyCredentialRegistration*)
                  credential;
      if (__builtin_available(macos 13.5, *)) {
        switch (platformCredential.attachment) {
          case ASAuthorizationPublicKeyCredentialAttachmentCrossPlatform:
            authenticatorAttachment.emplace(u"cross-platform"_ns);
            break;
          case ASAuthorizationPublicKeyCredentialAttachmentPlatform:
            authenticatorAttachment.emplace(u"platform"_ns);
            break;
          default:
            break;
        }
      }
      if (__builtin_available(macos 14.0, *)) {
        if (platformCredential.largeBlob) {
          largeBlobSupported.emplace(platformCredential.largeBlob.isSupported);
        }
      }
      if (__builtin_available(macos 15.0, *)) {
        if (platformCredential.prf) {
          prfSupported.emplace(platformCredential.prf.isSupported);
          if (platformCredential.prf.first) {
            prfFirst.emplace(NSDataToArray(platformCredential.prf.first));
          }
          if (platformCredential.prf.second) {
            prfSecond.emplace(NSDataToArray(platformCredential.prf.second));
          }
        }
      }
    } else if (
        [credential
            isKindOfClass:
                [ASAuthorizationSecurityKeyPublicKeyCredentialRegistration
                    class]]) {
      // The platform didn't tell us what transport was used, but we know it
      // wasn't the internal transport. The transport response is not signed by
      // the authenticator. It represents the "transports that the authenticator
      // is believed to support, or an empty sequence if the information is
      // unavailable". We believe macOS supports usb, so we return usb.
      transports.AppendElement(u"usb"_ns);
      authenticatorAttachment.emplace(u"cross-platform"_ns);
      if (__builtin_available(macos 26.4, *)) {
        ASAuthorizationSecurityKeyPublicKeyCredentialRegistration*
            securityKeyCredential =
                (ASAuthorizationSecurityKeyPublicKeyCredentialRegistration*)
                    credential;
        if (securityKeyCredential.prf) {
          prfSupported.emplace(securityKeyCredential.prf.isSupported);
          if (securityKeyCredential.prf.first) {
            prfFirst.emplace(NSDataToArray(securityKeyCredential.prf.first));
          }
          if (securityKeyCredential.prf.second) {
            prfSecond.emplace(NSDataToArray(securityKeyCredential.prf.second));
          }
        }
      }
    }
    mCallback->FinishMakeCredential(
        mTransactionId, std::move(rawAttestationObject),
        std::move(credentialId), std::move(transports),
        std::move(authenticatorAttachment), largeBlobSupported, prfSupported,
        std::move(prfFirst), std::move(prfSecond));
  } else if ([authorization.credential
                 conformsToProtocol:
                     @protocol(ASAuthorizationPublicKeyCredentialAssertion)]) {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("MacOSAuthenticatorRequestDelegate::didCompleteWithAuthorization: "
             "got assertion"));
    id<ASAuthorizationPublicKeyCredentialAssertion> credential =
        (id<ASAuthorizationPublicKeyCredentialAssertion>)
            authorization.credential;
    nsTArray<uint8_t> credentialId(NSDataToArray(credential.credentialID));
    nsTArray<uint8_t> signature(NSDataToArray(credential.signature));
    nsTArray<uint8_t> rawAuthenticatorData(
        NSDataToArray(credential.rawAuthenticatorData));
    nsTArray<uint8_t> userHandle(NSDataToArray(credential.userID));
    mozilla::Maybe<nsString> authenticatorAttachment;
    mozilla::Maybe<bool> usedAppId;
    mozilla::Maybe<nsTArray<uint8_t>> largeBlobValue;
    mozilla::Maybe<bool> largeBlobWritten;
    mozilla::Maybe<nsTArray<uint8_t>> prfFirst;
    mozilla::Maybe<nsTArray<uint8_t>> prfSecond;
    if ([credential
            isKindOfClass:[ASAuthorizationPlatformPublicKeyCredentialAssertion
                              class]]) {
      ASAuthorizationPlatformPublicKeyCredentialAssertion* platformCredential =
          (ASAuthorizationPlatformPublicKeyCredentialAssertion*)credential;
      if (__builtin_available(macos 13.5, *)) {
        switch (platformCredential.attachment) {
          case ASAuthorizationPublicKeyCredentialAttachmentCrossPlatform:
            authenticatorAttachment.emplace(u"cross-platform"_ns);
            break;
          case ASAuthorizationPublicKeyCredentialAttachmentPlatform:
            authenticatorAttachment.emplace(u"platform"_ns);
            break;
          default:
            break;
        }
      }
      if (__builtin_available(macos 14.0, *)) {
        if (platformCredential.largeBlob) {
          if (platformCredential.largeBlob.readData) {
            largeBlobValue.emplace(
                NSDataToArray(platformCredential.largeBlob.readData));
          } else {
            largeBlobWritten.emplace(platformCredential.largeBlob.didWrite);
          }
        }
      }
      if (__builtin_available(macos 15.0, *)) {
        if (platformCredential.prf) {
          if (platformCredential.prf.first) {
            prfFirst.emplace(NSDataToArray(platformCredential.prf.first));
          }
          if (platformCredential.prf.second) {
            prfSecond.emplace(NSDataToArray(platformCredential.prf.second));
          }
        }
      }
    } else if ([credential
                   isKindOfClass:
                       [ASAuthorizationSecurityKeyPublicKeyCredentialAssertion
                           class]]) {
      ASAuthorizationSecurityKeyPublicKeyCredentialAssertion*
          securityKeyCredential =
              (ASAuthorizationSecurityKeyPublicKeyCredentialAssertion*)
                  credential;
      if (__builtin_available(macos 14.5, *)) {
        usedAppId.emplace(securityKeyCredential.appID);
      }
      if (__builtin_available(macos 26.4, *)) {
        if (securityKeyCredential.prf) {
          if (securityKeyCredential.prf.first) {
            prfFirst.emplace(NSDataToArray(securityKeyCredential.prf.first));
          }
          if (securityKeyCredential.prf.second) {
            prfSecond.emplace(NSDataToArray(securityKeyCredential.prf.second));
          }
        }
      }
      authenticatorAttachment.emplace(u"cross-platform"_ns);
    }
    mCallback->FinishGetAssertion(
        mTransactionId, std::move(credentialId), std::move(signature),
        std::move(rawAuthenticatorData), std::move(userHandle),
        std::move(authenticatorAttachment), usedAppId,
        std::move(largeBlobValue), largeBlobWritten, std::move(prfFirst),
        std::move(prfSecond));
  } else {
    MOZ_LOG(
        gMacOSWebAuthnServiceLog, mozilla::LogLevel::Error,
        ("MacOSAuthenticatorRequestDelegate::didCompleteWithAuthorization: "
         "authorization.credential is neither registration nor assertion!"));
    MOZ_ASSERT_UNREACHABLE(
        "should have ASAuthorizationPublicKeyCredentialRegistration or "
        "ASAuthorizationPublicKeyCredentialAssertion");
  }
  mCallback = nullptr;
}

- (void)authorizationController:(ASAuthorizationController*)controller
           didCompleteWithError:(NSError*)error {
  nsAutoString errorDescription;
  nsCocoaUtils::GetStringForNSString(error.localizedDescription,
                                     errorDescription);
  nsAutoString errorDomain;
  nsCocoaUtils::GetStringForNSString(error.domain, errorDomain);
  MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Warning,
          ("MacOSAuthenticatorRequestDelegate::didCompleteWithError: domain "
           "'%s' code %ld (%s)",
           NS_ConvertUTF16toUTF8(errorDomain).get(), error.code,
           NS_ConvertUTF16toUTF8(errorDescription).get()));
  nsresult rv = NS_ERROR_DOM_NOT_ALLOWED_ERR;
  // For some reason, the error for "the credential used in a registration was
  // on the exclude list" is in the "WKErrorDomain" domain with code 8, which
  // is presumably WKErrorDuplicateCredential.
  const NSInteger WKErrorDuplicateCredential = 8;
  if (errorDomain.EqualsLiteral("WKErrorDomain") &&
      error.code == WKErrorDuplicateCredential) {
    rv = NS_ERROR_DOM_INVALID_STATE_ERR;
  } else if (error.domain == ASAuthorizationErrorDomain) {
    switch (error.code) {
      case ASAuthorizationErrorCanceled:
        rv = NS_ERROR_DOM_NOT_ALLOWED_ERR;
        break;
      case ASAuthorizationErrorMatchedExcludedCredential:
        rv = NS_ERROR_DOM_INVALID_STATE_ERR;
        break;
      case ASAuthorizationErrorFailed:
        // The message is right, but it's not about indexeddb.
        // See https://webidl.spec.whatwg.org/#constrainterror
        rv = NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
        break;
      case ASAuthorizationErrorUnknown:
        rv = NS_ERROR_DOM_UNKNOWN_ERR;
        break;
      default:
        // rv already has a default value
        break;
    }
  }
  mCallback->AbortTransaction(mTransactionId, rv);
  mCallback = nullptr;
}
@end

@implementation MacOSAuthenticatorPresentationContextProvider
@synthesize window = window;

- (ASPresentationAnchor)presentationAnchorForAuthorizationController:
    (ASAuthorizationController*)controller {
  return window;
}
@end

namespace mozilla::dom {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
// Given a browsingContextId, attempts to determine the NSWindow associated
// with that browser.
nsresult BrowsingContextIdToNSWindow(uint64_t browsingContextId,
                                     NSWindow** window) {
  *window = nullptr;
  RefPtr<BrowsingContext> browsingContext(
      BrowsingContext::Get(browsingContextId));
  if (!browsingContext) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  CanonicalBrowsingContext* canonicalBrowsingContext =
      browsingContext->Canonical();
  if (!canonicalBrowsingContext) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsCOMPtr<nsIWidget> widget(
      canonicalBrowsingContext->GetParentProcessWidgetContaining());
  if (!widget) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *window = static_cast<NSWindow*>(widget->GetNativeData(NS_NATIVE_WINDOW));
  return NS_OK;
}
#pragma clang diagnostic pop

already_AddRefed<nsIWebAuthnService> NewMacOSWebAuthnServiceIfAvailable() {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!StaticPrefs::security_webauthn_enable_macos_passkeys()) {
    MOZ_LOG(
        gMacOSWebAuthnServiceLog, LogLevel::Debug,
        ("macOS platform support for webauthn (passkeys) disabled by pref"));
    return nullptr;
  }
  // This code checks for the entitlement
  // 'com.apple.developer.web-browser.public-key-credential', which must be
  // true to be able to use the platform APIs.
  CFTypeRefPtr<SecTaskRef> entitlementTask(
      CFTypeRefPtr<SecTaskRef>::WrapUnderCreateRule(
          SecTaskCreateFromSelf(nullptr)));
  CFTypeRefPtr<CFBooleanRef> entitlementValue(
      CFTypeRefPtr<CFBooleanRef>::WrapUnderCreateRule(
          reinterpret_cast<CFBooleanRef>(SecTaskCopyValueForEntitlement(
              entitlementTask.get(),
              CFSTR("com.apple.developer.web-browser.public-key-credential"),
              nullptr))));
  if (!entitlementValue || !CFBooleanGetValue(entitlementValue.get())) {
    MOZ_LOG(
        gMacOSWebAuthnServiceLog, LogLevel::Warning,
        ("entitlement com.apple.developer.web-browser.public-key-credential "
         "not present: platform passkey APIs will not be available"));
    return nullptr;
  }
  nsCOMPtr<nsIWebAuthnService> service(new MacOSWebAuthnService());
  return service.forget();
}

void MacOSWebAuthnService::AbortTransaction(nsresult aError) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mRegisterPromise) {
    (void)mRegisterPromise->Reject(aError);
    mRegisterPromise = nullptr;
  }
  if (mSignPromise) {
    (void)mSignPromise->Reject(aError);
    mSignPromise = nullptr;
  }
  ReleasePlatformResources();
}

void MacOSWebAuthnService::AbortTransaction(uint64_t aTransactionId,
                                            nsresult aError) {
  // This is invoked from the platform delegate callback, which may not be
  // running on the main thread.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "MacOSWebAuthnService::AbortTransaction",
      [self = RefPtr{this}, aTransactionId, aError]() {
        MOZ_ASSERT(NS_IsMainThread());
        if (self->mTransactionId.isNothing() ||
            self->mTransactionId.ref() != aTransactionId) {
          // The transaction was superseded or cancelled before this
          // delegate callback fired; the result belongs to a stale
          // controller.
          return;
        }
        self->AbortTransaction(aError);
      }));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
NS_IMPL_ISUPPORTS(MacOSWebAuthnService, nsIWebAuthnService)
#pragma clang diagnostic pop

NS_IMETHODIMP
MacOSWebAuthnService::MakeCredential(uint64_t aTransactionId,
                                     uint64_t aBrowsingContextId,
                                     nsIWebAuthnRegisterArgs* aArgs,
                                     nsIWebAuthnRegisterPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mTransactionId.isNothing(),
             "WebAuthnService should reset the platform service before "
             "dispatching MakeCredential");

  mTransactionId = Some(aTransactionId);
  mRegisterPromise = aPromise;

  nsAutoString rpId;
  (void)aArgs->GetRpId(rpId);
  NSString* rpIdNS = nsCocoaUtils::ToNSString(rpId);

  nsTArray<uint8_t> challenge;
  (void)aArgs->GetChallenge(challenge);
  NSData* challengeNS = [NSData dataWithBytes:challenge.Elements()
                                       length:challenge.Length()];

  nsTArray<uint8_t> userId;
  (void)aArgs->GetUserId(userId);
  NSData* userIdNS = [NSData dataWithBytes:userId.Elements()
                                    length:userId.Length()];

  nsAutoString userName;
  (void)aArgs->GetUserName(userName);
  NSString* userNameNS = nsCocoaUtils::ToNSString(userName);

  nsAutoString userDisplayName;
  (void)aArgs->GetUserDisplayName(userDisplayName);
  NSString* userDisplayNameNS = nsCocoaUtils::ToNSString(userDisplayName);

  nsTArray<int32_t> coseAlgs;
  (void)aArgs->GetCoseAlgs(coseAlgs);
  NSMutableArray* credentialParameters = [[NSMutableArray alloc] init];
  for (const auto& coseAlg : coseAlgs) {
    ASAuthorizationPublicKeyCredentialParameters* credentialParameter =
        [[ASAuthorizationPublicKeyCredentialParameters alloc]
            initWithAlgorithm:coseAlg];
    [credentialParameters addObject:credentialParameter];
  }

  nsTArray<nsTArray<uint8_t>> excludeList;
  (void)aArgs->GetExcludeList(excludeList);
  nsTArray<uint8_t> excludeListTransports;
  (void)aArgs->GetExcludeListTransports(excludeListTransports);
  if (excludeList.Length() != excludeListTransports.Length()) {
    AbortTransaction(NS_ERROR_INVALID_ARG);
    return NS_OK;
  }

  Maybe<ASAuthorizationPublicKeyCredentialUserVerificationPreference>
      userVerificationPreference = Nothing();
  nsAutoString userVerification;
  (void)aArgs->GetUserVerification(userVerification);
  // This mapping needs to be reviewed if values are added to the
  // UserVerificationRequirement enum.
  static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
  if (userVerification.EqualsLiteral(
          MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED)) {
    userVerificationPreference.emplace(
        ASAuthorizationPublicKeyCredentialUserVerificationPreferenceRequired);
  } else if (userVerification.EqualsLiteral(
                 MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED)) {
    userVerificationPreference.emplace(
        ASAuthorizationPublicKeyCredentialUserVerificationPreferencePreferred);
  } else if (userVerification.EqualsLiteral(
                 MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_DISCOURAGED)) {
    userVerificationPreference.emplace(
        ASAuthorizationPublicKeyCredentialUserVerificationPreferenceDiscouraged);
  }

  // The API doesn't support attestation for platform passkeys, so this is
  // only used for security keys.
  ASAuthorizationPublicKeyCredentialAttestationKind attestationPreference;
  nsAutoString mozAttestationPreference;
  (void)aArgs->GetAttestationConveyancePreference(mozAttestationPreference);
  if (mozAttestationPreference.EqualsLiteral(
          MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_INDIRECT)) {
    attestationPreference =
        ASAuthorizationPublicKeyCredentialAttestationKindIndirect;
  } else if (mozAttestationPreference.EqualsLiteral(
                 MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_DIRECT)) {
    attestationPreference =
        ASAuthorizationPublicKeyCredentialAttestationKindDirect;
  } else if (mozAttestationPreference.EqualsLiteral(
                 MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_ENTERPRISE)) {
    attestationPreference =
        ASAuthorizationPublicKeyCredentialAttestationKindEnterprise;
  } else {
    attestationPreference =
        ASAuthorizationPublicKeyCredentialAttestationKindNone;
  }

  ASAuthorizationPublicKeyCredentialResidentKeyPreference residentKeyPreference;
  nsAutoString mozResidentKey;
  (void)aArgs->GetResidentKey(mozResidentKey);
  // This mapping needs to be reviewed if values are added to the
  // ResidentKeyRequirement enum.
  static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
  if (mozResidentKey.EqualsLiteral(
          MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_REQUIRED)) {
    residentKeyPreference =
        ASAuthorizationPublicKeyCredentialResidentKeyPreferenceRequired;
  } else if (mozResidentKey.EqualsLiteral(
                 MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_PREFERRED)) {
    residentKeyPreference =
        ASAuthorizationPublicKeyCredentialResidentKeyPreferencePreferred;
  } else {
    MOZ_ASSERT(mozResidentKey.EqualsLiteral(
        MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_DISCOURAGED));
    residentKeyPreference =
        ASAuthorizationPublicKeyCredentialResidentKeyPreferenceDiscouraged;
  }

  // Initialize the platform provider with the rpId.
  ASAuthorizationPlatformPublicKeyCredentialProvider* platformProvider =
      [[ASAuthorizationPlatformPublicKeyCredentialProvider alloc]
          initWithRelyingPartyIdentifier:rpIdNS];
  // Make a credential registration request with the challenge, userName,
  // and userId.
  ASAuthorizationPlatformPublicKeyCredentialRegistrationRequest*
      platformRegistrationRequest = [platformProvider
          createCredentialRegistrationRequestWithChallenge:challengeNS
                                                      name:userNameNS
                                                    userID:userIdNS];
  [platformProvider release];

  // The API doesn't support attestation for platform passkeys
  platformRegistrationRequest.attestationPreference =
      ASAuthorizationPublicKeyCredentialAttestationKindNone;
  if (userVerificationPreference.isSome()) {
    platformRegistrationRequest.userVerificationPreference =
        *userVerificationPreference;
  }

  // Initialize the cross-platform provider with the rpId.
  ASAuthorizationSecurityKeyPublicKeyCredentialProvider* crossPlatformProvider =
      [[ASAuthorizationSecurityKeyPublicKeyCredentialProvider alloc]
          initWithRelyingPartyIdentifier:rpIdNS];
  // Make a credential registration request with the challenge,
  // userDisplayName, userName, and userId.
  ASAuthorizationSecurityKeyPublicKeyCredentialRegistrationRequest*
      crossPlatformRegistrationRequest = [crossPlatformProvider
          createCredentialRegistrationRequestWithChallenge:challengeNS
                                               displayName:userDisplayNameNS
                                                      name:userNameNS
                                                    userID:userIdNS];
  [crossPlatformProvider release];
  crossPlatformRegistrationRequest.attestationPreference =
      attestationPreference;
  crossPlatformRegistrationRequest.credentialParameters = credentialParameters;
  crossPlatformRegistrationRequest.residentKeyPreference =
      residentKeyPreference;
  if (userVerificationPreference.isSome()) {
    crossPlatformRegistrationRequest.userVerificationPreference =
        *userVerificationPreference;
  }

  if (__builtin_available(macos 13.5, *)) {
    // Show the hybrid transport unless we have a non-empty hint list and
    // none of the hints are for the hybrid transport.
    bool hasHybridHint = false;
    nsTArray<nsString> hints;
    (void)aArgs->GetHints(hints);
    for (nsString& hint : hints) {
      if (hint.Equals(u"hybrid"_ns)) {
        hasHybridHint = true;
      }
    }
    platformRegistrationRequest.shouldShowHybridTransport =
        hints.Length() == 0 || hasHybridHint;
  }
  if (__builtin_available(macos 14.0, *)) {
    bool largeBlobSupportRequired;
    nsresult rv = aArgs->GetLargeBlobSupportRequired(&largeBlobSupportRequired);
    if (rv != NS_ERROR_NOT_AVAILABLE) {
      if (NS_FAILED(rv)) {
        AbortTransaction(rv);
        return NS_OK;
      }
      ASAuthorizationPublicKeyCredentialLargeBlobSupportRequirement
          largeBlobRequirement =
              largeBlobSupportRequired
                  ? ASAuthorizationPublicKeyCredentialLargeBlobSupportRequirementRequired
                  : ASAuthorizationPublicKeyCredentialLargeBlobSupportRequirementPreferred;
      platformRegistrationRequest.largeBlob =
          [[ASAuthorizationPublicKeyCredentialLargeBlobRegistrationInput alloc]
              initWithSupportRequirement:largeBlobRequirement];
    }
  }
  if (__builtin_available(macos 15.0, *)) {
    bool requestedPrf;
    (void)aArgs->GetPrf(&requestedPrf);
    if (requestedPrf) {
      NSData* saltInput1 = nil;
      NSData* saltInput2 = nil;
      nsTArray<uint8_t> prfInput1;
      nsresult rv = aArgs->GetPrfEvalFirst(prfInput1);
      if (rv != NS_ERROR_NOT_AVAILABLE) {
        if (NS_FAILED(rv)) {
          AbortTransaction(rv);
          return NS_OK;
        }
        saltInput1 = [NSData dataWithBytes:prfInput1.Elements()
                                    length:prfInput1.Length()];
      }
      nsTArray<uint8_t> prfInput2;
      rv = aArgs->GetPrfEvalSecond(prfInput2);
      if (rv != NS_ERROR_NOT_AVAILABLE) {
        if (NS_FAILED(rv)) {
          AbortTransaction(rv);
          return NS_OK;
        }
        saltInput2 = [NSData dataWithBytes:prfInput2.Elements()
                                    length:prfInput2.Length()];
      }
      ASAuthorizationPublicKeyCredentialPRFAssertionInputValues* prfInputs =
          [[ASAuthorizationPublicKeyCredentialPRFAssertionInputValues alloc]
              initWithSaltInput1:saltInput1
                      saltInput2:saltInput2];
      ASAuthorizationPublicKeyCredentialPRFRegistrationInput*
          prfRegistrationInput =
              [[ASAuthorizationPublicKeyCredentialPRFRegistrationInput alloc]
                  initWithInputValues:prfInputs];
      platformRegistrationRequest.prf = prfRegistrationInput;
      if (__builtin_available(macos 26.4, *)) {
        crossPlatformRegistrationRequest.prf = prfRegistrationInput;
      }
    }
  }

  nsTArray<uint8_t> clientDataHash;
  nsresult rv = aArgs->GetClientDataHash(clientDataHash);
  if (NS_FAILED(rv)) {
    AbortTransaction(rv);
    return NS_OK;
  }
  nsAutoString authenticatorAttachment;
  rv = aArgs->GetAuthenticatorAttachment(authenticatorAttachment);
  if (NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE) {
    AbortTransaction(rv);
    return NS_OK;
  }
  NSMutableArray* requests = [[NSMutableArray alloc] init];
  if (authenticatorAttachment.EqualsLiteral(
          MOZ_WEBAUTHN_AUTHENTICATOR_ATTACHMENT_PLATFORM)) {
    [requests addObject:platformRegistrationRequest];
  } else if (authenticatorAttachment.EqualsLiteral(
                 MOZ_WEBAUTHN_AUTHENTICATOR_ATTACHMENT_CROSS_PLATFORM)) {
    [requests addObject:crossPlatformRegistrationRequest];
  } else {
    // Regarding the value of authenticator attachment, according to the
    // specification, "client platforms MUST ignore unknown values,
    // treating an unknown value as if the member does not exist".
    [requests addObject:platformRegistrationRequest];
    [requests addObject:crossPlatformRegistrationRequest];
  }
  PerformRequests(aTransactionId, requests, std::move(clientDataHash),
                  std::move(excludeList), std::move(excludeListTransports),
                  aBrowsingContextId);
  return NS_OK;
}

void MacOSWebAuthnService::PerformRequests(
    uint64_t aTransactionId, NSArray<ASAuthorizationRequest*>* aRequests,
    nsTArray<uint8_t>&& aClientDataHash,
    nsTArray<nsTArray<uint8_t>>&& aCredentialList,
    nsTArray<uint8_t>&& aCredentialListTransports,
    uint64_t aBrowsingContextId) {
  MOZ_ASSERT(NS_IsMainThread());
  // Create a MacOSAuthorizationController and initialize it with the requests.
  MOZ_ASSERT(!mAuthorizationController);
  mAuthorizationController = [[MacOSAuthorizationController alloc]
      initWithAuthorizationRequests:aRequests];
  [mAuthorizationController
              stashClientDataHash:std::move(aClientDataHash)
                andCredentialList:std::move(aCredentialList)
      andCredentialListTransports:std::move(aCredentialListTransports)];

  // Set up the delegate to run when the operation completes.
  MOZ_ASSERT(!mRequestDelegate);
  mRequestDelegate = [[MacOSAuthenticatorRequestDelegate alloc] init];
  [mRequestDelegate setCallback:this transactionId:aTransactionId];
  mAuthorizationController.delegate = mRequestDelegate;

  // Create a presentation context provider so the API knows which window
  // made the request.
  NSWindow* window = nullptr;
  nsresult rv = BrowsingContextIdToNSWindow(aBrowsingContextId, &window);
  if (NS_FAILED(rv)) {
    AbortTransaction(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  MOZ_ASSERT(!mPresentationContextProvider);
  mPresentationContextProvider =
      [[MacOSAuthenticatorPresentationContextProvider alloc] init];
  mPresentationContextProvider.window = window;
  mAuthorizationController.presentationContextProvider =
      mPresentationContextProvider;

  // Finally, perform the request.
  [mAuthorizationController performRequests];
}

void MacOSWebAuthnService::FinishMakeCredential(
    uint64_t aTransactionId, nsTArray<uint8_t>&& aRawAttestationObject,
    nsTArray<uint8_t>&& aCredentialId, nsTArray<nsString>&& aTransports,
    Maybe<nsString>&& aAuthenticatorAttachment, Maybe<bool> aLargeBlobSupported,
    Maybe<bool> aPrfSupported, Maybe<nsTArray<uint8_t>>&& aPrfFirst,
    Maybe<nsTArray<uint8_t>>&& aPrfSecond) {
  // This is invoked from the platform delegate callback, which may not be
  // running on the main thread.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "MacOSWebAuthnService::FinishMakeCredential",
      [self = RefPtr{this}, aTransactionId,
       rawAttestationObject = std::move(aRawAttestationObject),
       credentialId = std::move(aCredentialId),
       transports = std::move(aTransports),
       authenticatorAttachment = std::move(aAuthenticatorAttachment),
       largeBlobSupported = aLargeBlobSupported, prfSupported = aPrfSupported,
       prfFirst = std::move(aPrfFirst), prfSecond = std::move(aPrfSecond)]() {
        MOZ_ASSERT(NS_IsMainThread());
        if (self->mTransactionId.isNothing() ||
            self->mTransactionId.ref() != aTransactionId) {
          // The transaction was superseded or cancelled before this
          // delegate callback fired; the result belongs to a stale
          // controller.
          return;
        }
        if (!self->mRegisterPromise) {
          return;
        }
        RefPtr<WebAuthnRegisterResult> result(new WebAuthnRegisterResult(
            rawAttestationObject, Nothing(), credentialId, transports,
            authenticatorAttachment, largeBlobSupported, prfSupported, prfFirst,
            prfSecond));
        (void)self->mRegisterPromise->Resolve(result);
        self->mRegisterPromise = nullptr;
        self->ReleasePlatformResources();
      }));
}

NS_IMETHODIMP
MacOSWebAuthnService::GetAssertion(uint64_t aTransactionId,
                                   uint64_t aBrowsingContextId,
                                   nsIWebAuthnSignArgs* aArgs,
                                   nsIWebAuthnSignPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mTransactionId.isNothing(),
             "WebAuthnService should reset the platform service before "
             "dispatching GetAssertion");

  mTransactionId = Some(aTransactionId);
  mSignPromise = aPromise;

  nsAutoString rpId;
  (void)aArgs->GetRpId(rpId);
  NSString* rpIdNS = nsCocoaUtils::ToNSString(rpId);

  nsTArray<uint8_t> challenge;
  (void)aArgs->GetChallenge(challenge);
  NSData* challengeNS = [NSData dataWithBytes:challenge.Elements()
                                       length:challenge.Length()];

  nsTArray<nsTArray<uint8_t>> allowList;
  nsTArray<uint8_t> allowListTransports;
  (void)aArgs->GetAllowList(allowList);
  (void)aArgs->GetAllowListTransports(allowListTransports);
  // Compute the union of the transport sets.
  uint8_t transports = 0;
  for (uint8_t credTransports : allowListTransports) {
    if (credTransports == 0) {
      // treat the empty transport set as "all transports".
      transports = ~0;
      break;
    }
    transports |= credTransports;
  }

  NSMutableArray* platformAllowedCredentials = [[NSMutableArray alloc] init];
  for (const auto& allowedCredentialId : allowList) {
    NSData* allowedCredentialIdNS =
        [NSData dataWithBytes:allowedCredentialId.Elements()
                       length:allowedCredentialId.Length()];
    ASAuthorizationPlatformPublicKeyCredentialDescriptor* allowedCredential =
        [[ASAuthorizationPlatformPublicKeyCredentialDescriptor alloc]
            initWithCredentialID:allowedCredentialIdNS];
    [platformAllowedCredentials addObject:allowedCredential];
  }
  const Class securityKeyPublicKeyCredentialDescriptorClass = NSClassFromString(
      @"ASAuthorizationSecurityKeyPublicKeyCredentialDescriptor");
  NSArray<ASAuthorizationSecurityKeyPublicKeyCredentialDescriptor*>*
      crossPlatformAllowedCredentials =
          CredentialListsToCredentialDescriptorArray(
              allowList, allowListTransports,
              securityKeyPublicKeyCredentialDescriptorClass);

  Maybe<ASAuthorizationPublicKeyCredentialUserVerificationPreference>
      userVerificationPreference = Nothing();
  nsAutoString userVerification;
  (void)aArgs->GetUserVerification(userVerification);
  // This mapping needs to be reviewed if values are added to the
  // UserVerificationRequirement enum.
  static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
  if (userVerification.EqualsLiteral(
          MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED)) {
    userVerificationPreference.emplace(
        ASAuthorizationPublicKeyCredentialUserVerificationPreferenceRequired);
  } else if (userVerification.EqualsLiteral(
                 MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED)) {
    userVerificationPreference.emplace(
        ASAuthorizationPublicKeyCredentialUserVerificationPreferencePreferred);
  } else if (userVerification.EqualsLiteral(
                 MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_DISCOURAGED)) {
    userVerificationPreference.emplace(
        ASAuthorizationPublicKeyCredentialUserVerificationPreferenceDiscouraged);
  }

  // Initialize the platform provider with the rpId.
  ASAuthorizationPlatformPublicKeyCredentialProvider* platformProvider =
      [[ASAuthorizationPlatformPublicKeyCredentialProvider alloc]
          initWithRelyingPartyIdentifier:rpIdNS];
  // Make a credential assertion request with the challenge.
  ASAuthorizationPlatformPublicKeyCredentialAssertionRequest*
      platformAssertionRequest = [platformProvider
          createCredentialAssertionRequestWithChallenge:challengeNS];
  [platformProvider release];
  platformAssertionRequest.allowedCredentials = platformAllowedCredentials;
  if (userVerificationPreference.isSome()) {
    platformAssertionRequest.userVerificationPreference =
        *userVerificationPreference;
  }
  if (__builtin_available(macos 13.5, *)) {
    // Show the hybrid transport option if (1) none of the allowlist
    // credentials list transports, or (2) at least one allow list entry
    // lists the hybrid transport, or (3) the request has the hybrid hint.
    bool shouldShowHybridTransport =
        !transports ||
        (transports & MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_HYBRID);
    nsTArray<nsString> hints;
    (void)aArgs->GetHints(hints);
    for (nsString& hint : hints) {
      if (hint.Equals(u"hybrid"_ns)) {
        shouldShowHybridTransport = true;
      }
    }
    platformAssertionRequest.shouldShowHybridTransport =
        shouldShowHybridTransport;
  }

  // Initialize the cross-platform provider with the rpId.
  ASAuthorizationSecurityKeyPublicKeyCredentialProvider* crossPlatformProvider =
      [[ASAuthorizationSecurityKeyPublicKeyCredentialProvider alloc]
          initWithRelyingPartyIdentifier:rpIdNS];
  // Make a credential assertion request with the challenge.
  ASAuthorizationSecurityKeyPublicKeyCredentialAssertionRequest*
      crossPlatformAssertionRequest = [crossPlatformProvider
          createCredentialAssertionRequestWithChallenge:challengeNS];
  [crossPlatformProvider release];
  crossPlatformAssertionRequest.allowedCredentials =
      crossPlatformAllowedCredentials;
  if (userVerificationPreference.isSome()) {
    crossPlatformAssertionRequest.userVerificationPreference =
        *userVerificationPreference;
  }

  if (__builtin_available(macos 14.0, *)) {
    nsTArray<uint8_t> largeBlobWrite;
    bool largeBlobRead;
    nsresult rv = aArgs->GetLargeBlobRead(&largeBlobRead);
    if (rv != NS_ERROR_NOT_AVAILABLE) {
      if (NS_FAILED(rv)) {
        AbortTransaction(rv);
        return NS_OK;
      }
      if (largeBlobRead) {
        platformAssertionRequest
            .largeBlob = [[ASAuthorizationPublicKeyCredentialLargeBlobAssertionInput
            alloc]
            initWithOperation:
                ASAuthorizationPublicKeyCredentialLargeBlobAssertionOperationRead];
      } else {
        rv = aArgs->GetLargeBlobWrite(largeBlobWrite);
        if (rv != NS_ERROR_NOT_AVAILABLE) {
          if (NS_FAILED(rv)) {
            AbortTransaction(rv);
            return NS_OK;
          }
          ASAuthorizationPublicKeyCredentialLargeBlobAssertionInput*
              largeBlobAssertionInput =
                  [[ASAuthorizationPublicKeyCredentialLargeBlobAssertionInput
                      alloc]
                      initWithOperation:
                          ASAuthorizationPublicKeyCredentialLargeBlobAssertionOperationWrite];
          // We need to fully form the input before assigning it to
          // platformAssertionRequest.largeBlob.  See
          // https://bugs.webkit.org/show_bug.cgi?id=276961
          largeBlobAssertionInput.dataToWrite =
              [NSData dataWithBytes:largeBlobWrite.Elements()
                             length:largeBlobWrite.Length()];
          platformAssertionRequest.largeBlob = largeBlobAssertionInput;
        }
      }
    }
  }

  if (__builtin_available(macos 14.5, *)) {
    nsString appId;
    nsresult rv = aArgs->GetAppId(appId);
    if (rv != NS_ERROR_NOT_AVAILABLE) {  // AppID is set
      if (NS_FAILED(rv)) {
        AbortTransaction(rv);
        return NS_OK;
      }
      crossPlatformAssertionRequest.appID = nsCocoaUtils::ToNSString(appId);
    }
  }

  if (__builtin_available(macos 15.0, *)) {
    bool requestedPrf;
    (void)aArgs->GetPrf(&requestedPrf);
    if (requestedPrf) {
      NSData* saltInput1 = nil;
      NSData* saltInput2 = nil;
      nsTArray<uint8_t> prfInput1;
      nsresult rv = aArgs->GetPrfEvalFirst(prfInput1);
      if (rv != NS_ERROR_NOT_AVAILABLE) {
        if (NS_FAILED(rv)) {
          AbortTransaction(rv);
          return NS_OK;
        }
        saltInput1 = [NSData dataWithBytes:prfInput1.Elements()
                                    length:prfInput1.Length()];
      }
      nsTArray<uint8_t> prfInput2;
      rv = aArgs->GetPrfEvalSecond(prfInput2);
      if (rv != NS_ERROR_NOT_AVAILABLE) {
        if (NS_FAILED(rv)) {
          AbortTransaction(rv);
          return NS_OK;
        }
        saltInput2 = [NSData dataWithBytes:prfInput2.Elements()
                                    length:prfInput2.Length()];
      }
      ASAuthorizationPublicKeyCredentialPRFAssertionInputValues* prfInputs =
          [[ASAuthorizationPublicKeyCredentialPRFAssertionInputValues alloc]
              initWithSaltInput1:saltInput1
                      saltInput2:saltInput2];

      NSDictionary<NSData*,
                   ASAuthorizationPublicKeyCredentialPRFAssertionInputValues*>*
          prfPerCredentialInputs = ConstructPrfEvalByCredentialEntries(aArgs);
      ASAuthorizationPublicKeyCredentialPRFAssertionInput* prfAssertionInput =
          [[ASAuthorizationPublicKeyCredentialPRFAssertionInput alloc]
                   initWithInputValues:prfInputs
              perCredentialInputValues:prfPerCredentialInputs];
      platformAssertionRequest.prf = prfAssertionInput;
      if (__builtin_available(macos 26.4, *)) {
        crossPlatformAssertionRequest.prf = prfAssertionInput;
      }
    }
  }

  nsTArray<uint8_t> clientDataHash;
  nsresult rv = aArgs->GetClientDataHash(clientDataHash);
  if (NS_FAILED(rv)) {
    AbortTransaction(rv);
    return NS_OK;
  }
  // allowList and allowListTransports won't actually get used.
  PerformRequests(aTransactionId,
                  @[ platformAssertionRequest, crossPlatformAssertionRequest ],
                  std::move(clientDataHash), std::move(allowList),
                  std::move(allowListTransports), aBrowsingContextId);
  return NS_OK;
}

void MacOSWebAuthnService::FinishGetAssertion(
    uint64_t aTransactionId, nsTArray<uint8_t>&& aCredentialId,
    nsTArray<uint8_t>&& aSignature, nsTArray<uint8_t>&& aAuthenticatorData,
    nsTArray<uint8_t>&& aUserHandle, Maybe<nsString>&& aAuthenticatorAttachment,
    Maybe<bool> aUsedAppId, Maybe<nsTArray<uint8_t>>&& aLargeBlobValue,
    Maybe<bool> aLargeBlobWritten, Maybe<nsTArray<uint8_t>>&& aPrfFirst,
    Maybe<nsTArray<uint8_t>>&& aPrfSecond) {
  // This is invoked from the platform delegate callback, which may not be
  // running on the main thread.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "MacOSWebAuthnService::FinishGetAssertion",
      [self = RefPtr{this}, aTransactionId,
       credentialId = std::move(aCredentialId),
       signature = std::move(aSignature),
       authenticatorData = std::move(aAuthenticatorData),
       userHandle = std::move(aUserHandle),
       authenticatorAttachment = std::move(aAuthenticatorAttachment),
       usedAppId = aUsedAppId, largeBlobValue = std::move(aLargeBlobValue),
       largeBlobWritten = aLargeBlobWritten, prfFirst = std::move(aPrfFirst),
       prfSecond = std::move(aPrfSecond)]() {
        MOZ_ASSERT(NS_IsMainThread());
        if (self->mTransactionId.isNothing() ||
            self->mTransactionId.ref() != aTransactionId) {
          // The transaction was superseded or cancelled before this
          // delegate callback fired; the result belongs to a stale
          // controller.
          return;
        }
        if (!self->mSignPromise) {
          return;
        }
        RefPtr<WebAuthnSignResult> result(new WebAuthnSignResult(
            authenticatorData, Nothing(), credentialId, signature, userHandle,
            authenticatorAttachment, usedAppId, largeBlobValue,
            largeBlobWritten, prfFirst, prfSecond));
        (void)self->mSignPromise->Resolve(result);
        self->mSignPromise = nullptr;
        self->ReleasePlatformResources();
      }));
}

void MacOSWebAuthnService::ReleasePlatformResources() {
  MOZ_ASSERT(NS_IsMainThread());
  [mCredentialManager release];
  mCredentialManager = nil;
  [mAuthorizationController release];
  mAuthorizationController = nil;
  [mRequestDelegate release];
  mRequestDelegate = nil;
  [mPresentationContextProvider release];
  mPresentationContextProvider = nil;
  mTransactionId.reset();
}

NS_IMETHODIMP
MacOSWebAuthnService::Reset() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mAuthorizationController) {
    [mAuthorizationController cancel];
  }
  AbortTransaction(NS_ERROR_DOM_ABORT_ERR);
  return NS_OK;
}

NS_IMETHODIMP
MacOSWebAuthnService::GetIsUVPAA(bool* aAvailable) {
  *aAvailable = true;
  return NS_OK;
}

NS_IMETHODIMP
MacOSWebAuthnService::Cancel(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mTransactionId.isSome() && mTransactionId.ref() == aTransactionId) {
    Reset();
  }
  return NS_OK;
}

NS_IMETHODIMP
MacOSWebAuthnService::HasPendingConditionalGet(uint64_t aBrowsingContextId,
                                               const nsAString& aOrigin,
                                               uint64_t* aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  *aRv = 0;
  return NS_OK;
}

NS_IMETHODIMP
MacOSWebAuthnService::GetAutoFillEntries(
    uint64_t aTransactionId, nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  aCallback->Reject(NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

NS_IMETHODIMP
MacOSWebAuthnService::GetAutoFillEntriesForRpId(
    const nsAString& aRpId, const nsTArray<nsTArray<uint8_t>>& aAllowList,
    nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  // Apple's completion blocks aren't guaranteed to run on the main thread,
  // but the callback is a JS-implemented XPCOM object that must only be
  // released (and invoked) on the main thread. Wrap it in a main-thread
  // pointer handle and dispatch the resolve/reject back to the main thread.
  nsMainThreadPtrHandle<nsIWebAuthnAutoFillEntriesCallback> callback(
      new nsMainThreadPtrHolder<nsIWebAuthnAutoFillEntriesCallback>(
          "MacOSWebAuthnService::GetAutoFillEntriesForRpId::callback",
          aCallback));
  nsString rpId(aRpId);
  auto credentialsCompletionHandler =
      ^(NSArray<ASAuthorizationWebBrowserPlatformPublicKeyCredential*>*
            credentials) {
        nsTArray<RefPtr<nsIWebAuthnAutoFillEntry>> entries;
        for (NSUInteger i = 0; i < credentials.count; i++) {
          const auto& credential = credentials[i];
          nsAutoString userName;
          nsCocoaUtils::GetStringForNSString(credential.name, userName);
          nsAutoString credRpId;
          nsCocoaUtils::GetStringForNSString(credential.relyingParty, credRpId);
          entries.AppendElement(new WebAuthnAutoFillEntry(
              nsIWebAuthnAutoFillEntry::PROVIDER_PLATFORM_MACOS, userName,
              credRpId, NSDataToArray(credential.credentialID)));
        }
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "MacOSWebAuthnService::GetAutoFillEntriesForRpId::resolve",
            [callback, entries = std::move(entries)]() mutable {
              callback->Resolve(entries);
            }));
      };
  RefPtr<MacOSWebAuthnService> self = this;
  auto authorizationHandler = ^(
      ASAuthorizationWebBrowserPublicKeyCredentialManagerAuthorizationState
          authorizationState) {
    if (authorizationState ==
        ASAuthorizationWebBrowserPublicKeyCredentialManagerAuthorizationStateAuthorized) {
      [self->mCredentialManager
          platformCredentialsForRelyingParty:nsCocoaUtils::ToNSString(rpId)
                           completionHandler:credentialsCompletionHandler];
    } else {
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "MacOSWebAuthnService::GetAutoFillEntriesForRpId::reject",
          [callback]() { callback->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR); }));
    }
  };
  if (!mCredentialManager) {
    mCredentialManager =
        [[ASAuthorizationWebBrowserPublicKeyCredentialManager alloc] init];
  }
  [mCredentialManager
      requestAuthorizationForPublicKeyCredentials:authorizationHandler];

  return NS_OK;
}

NS_IMETHODIMP
MacOSWebAuthnService::SelectAutoFillEntry(
    uint64_t aTransactionId, const nsTArray<uint8_t>& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
MacOSWebAuthnService::ResumeConditionalGet(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
MacOSWebAuthnService::PinCallback(uint64_t aTransactionId,
                                  const nsACString& aPin) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::SetHasAttestationConsent(uint64_t aTransactionId,
                                               bool aHasConsent) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::SelectionCallback(uint64_t aTransactionId,
                                        uint64_t aIndex) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::AddVirtualAuthenticator(
    const nsACString& aProtocol, const nsACString& aTransport,
    bool aHasResidentKey, bool aHasUserVerification, bool aIsUserConsenting,
    bool aIsUserVerified, nsACString& aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::RemoveVirtualAuthenticator(
    const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::AddCredential(const nsACString& aAuthenticatorId,
                                    const nsACString& aCredentialId,
                                    bool aIsResidentCredential,
                                    const nsACString& aRpId,
                                    const nsACString& aPrivateKey,
                                    const nsACString& aUserHandle,
                                    uint32_t aSignCount) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::GetCredentials(
    const nsACString& aAuthenticatorId,
    nsTArray<RefPtr<nsICredentialParameters>>& _aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::RemoveCredential(const nsACString& aAuthenticatorId,
                                       const nsACString& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::RemoveAllCredentials(const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::SetUserVerified(const nsACString& aAuthenticatorId,
                                      bool aIsUserVerified) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::Listen() {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MacOSWebAuthnService::RunCommand(const nsACString& aCmd) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

}  // namespace mozilla::dom

NS_ASSUME_NONNULL_END
