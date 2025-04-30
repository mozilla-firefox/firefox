/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <AuthenticationServices/ASAuthorizationSingleSignOnProvider.h>
#import <AuthenticationServices/AuthenticationServices.h>
#import <Foundation/Foundation.h>

#include <functional>  // For std::function

#include "MicrosoftEntraSSOUtils.h"
#include "nsIURI.h"
#include "nsHttp.h"
#include "nsHttpChannel.h"
#include "nsCocoaUtils.h"
#include "nsTHashMap.h"
#include "nsHashKeys.h"
#include "nsThreadUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/glean/NetwerkMetrics.h"

namespace {
static mozilla::LazyLogModule gMacOSWebAuthnServiceLog("macOSSingleSignOn");
}  // namespace

NS_ASSUME_NONNULL_BEGIN

// Delegate
API_AVAILABLE(macos(13.3))
@interface SSORequestDelegate : NSObject <ASAuthorizationControllerDelegate>
- (void)setCallback:(mozilla::net::MicrosoftEntraSSOUtils*)callback;
@end

namespace mozilla {
namespace net {

class API_AVAILABLE(macos(13.3)) MicrosoftEntraSSOUtils final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MicrosoftEntraSSOUtils)

  explicit MicrosoftEntraSSOUtils(nsHttpChannel* aChannel,
                                  std::function<void()>&& aResultCallback);
  bool AddMicrosoftEntraSSOInternal();
  void AddRequestHeader(const nsACString& aKey, const nsACString& aValue);
  void InvokeCallback();

 private:
  ~MicrosoftEntraSSOUtils();
  ASAuthorizationSingleSignOnProvider* mProvider;
  ASAuthorizationController* mAuthorizationController;
  SSORequestDelegate* mRequestDelegate;
  RefPtr<nsHttpChannel> mChannel;
  std::function<void()> mResultCallback;
  nsTHashMap<nsCStringHashKey, nsCString> mRequestHeaders;
};
}  // namespace net
}  // namespace mozilla

@implementation SSORequestDelegate {
  RefPtr<mozilla::net::MicrosoftEntraSSOUtils> mCallback;
}
- (void)setCallback:(mozilla::net::MicrosoftEntraSSOUtils*)callback {
  mCallback = callback;
}
- (void)authorizationController:(ASAuthorizationController*)controller
    didCompleteWithAuthorization:(ASAuthorization*)authorization {
  ASAuthorizationSingleSignOnCredential* ssoCredential =
      [authorization.credential
          isKindOfClass:[ASAuthorizationSingleSignOnCredential class]]
          ? (ASAuthorizationSingleSignOnCredential*)authorization.credential
          : nil;

  if (!ssoCredential) {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithAuthorization: "
             "should have ASAuthorizationSingleSignOnCredential"));
    mozilla::glean::network_sso::entra_success.Get("no_credential"_ns).Add(1);
    [self invokeCallbackOnMainThread];
    return;
  }

  NSHTTPURLResponse* authenticatedResponse =
      ssoCredential.authenticatedResponse;
  if (!authenticatedResponse) {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithAuthorization: "
             "authenticatedResponse is nil"));
    mozilla::glean::network_sso::entra_success.Get("invalid_cookie"_ns).Add(1);
    [self invokeCallbackOnMainThread];
    return;
  }

  NSDictionary* headers = authenticatedResponse.allHeaderFields;
  NSMutableString* headersString = [NSMutableString string];
  for (NSString* key in headers) {
    [headersString appendFormat:@"%@: %@\n", key, headers[key]];
  }
  MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
          ("SSORequestDelegate::didCompleteWithAuthorization: "
           "authenticatedResponse: \nStatus Code: %ld\nHeaders:\n%s",
           (long)authenticatedResponse.statusCode, [headersString UTF8String]));

  // An example format of ssoCookies:
  // sso_cookies:
  // {"device_headers":[
  // {"header":{"x-ms-DeviceCredential”:”…”},”tenant_id”:”…”}],
  // ”prt_headers":[{"header":{"x-ms-RefreshTokenCredential”:”…”},
  // ”home_account_id”:”….”}]}
  NSString* ssoCookies = headers[@"sso_cookies"];
  if (!ssoCookies) {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithAuthorization: "
             "authenticatedResponse is nil"));
    mozilla::glean::network_sso::entra_success.Get("invalid_cookie"_ns).Add(1);
    [self invokeCallbackOnMainThread];
    return;
  }
  NSError* err = nil;
  NSDictionary* ssoCookiesDict = [NSJSONSerialization
      JSONObjectWithData:[ssoCookies dataUsingEncoding:NSUTF8StringEncoding]
                 options:0
                   error:&err];

  if (err) {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithAuthorization: Error parsing "
             "JSON: %s",
             [[err localizedDescription] UTF8String]));
    mozilla::glean::network_sso::entra_success.Get("invalid_cookie"_ns).Add(1);
    [self invokeCallbackOnMainThread];
    return;
  }

  NSMutableArray* allHeaders = [NSMutableArray array];
  nsCString entraSuccessLabel;

  if (ssoCookiesDict[@"device_headers"]) {
    [allHeaders addObject:ssoCookiesDict[@"device_headers"]];
  } else {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithAuthorization: "
             "Missing device_headers"));
    entraSuccessLabel = "device_headers_missing"_ns;
  }

  if (ssoCookiesDict[@"prt_headers"]) {
    [allHeaders addObject:ssoCookiesDict[@"prt_headers"]];
  } else {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithAuthorization: "
             "Missing prt_headers"));
    entraSuccessLabel = "prt_headers_missing"_ns;
  }

  if (allHeaders.count == 0) {
    entraSuccessLabel = "both_headers_missing"_ns;
  }

  // We would like to have both device_headers and prt_headers before
  // attaching the headers
  if (allHeaders.count != 2) {
    MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithAuthorization: "
             "sso_cookies has missing headers"));
    mozilla::glean::network_sso::entra_success.Get(entraSuccessLabel).Add(1);
  } else {
    mozilla::glean::network_sso::entra_success.Get("success"_ns).Add(1);
  }

  // Append cookie headers retrieved from MS Broker
  for (NSArray* headerArray in allHeaders) {
    if (!headerArray) {
      continue;
    }
    for (NSDictionary* headerDict in headerArray) {
      NSDictionary* headers = headerDict[@"header"];
      if (!headers) {
        continue;
      }
      for (NSString* key in headers) {
        NSString* value = headers[key];
        if (!value) {
          continue;
        }
        nsAutoString nsKey;
        nsAutoString nsValue;
        mozilla::CopyNSStringToXPCOMString(key, nsKey);
        mozilla::CopyNSStringToXPCOMString(value, nsValue);
        mCallback->AddRequestHeader(NS_ConvertUTF16toUTF8(nsKey),
                                    NS_ConvertUTF16toUTF8(nsValue));
      }
    }
  }

  [self invokeCallbackOnMainThread];
}
- (void)invokeCallbackOnMainThread {
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "SSORequestDelegate::didCompleteWithAuthorization",
      [callback(mCallback)]() { callback->InvokeCallback(); }));
}

- (void)authorizationController:(ASAuthorizationController*)controller
           didCompleteWithError:(NSError*)error {
  nsAutoString errorDescription;
  nsAutoString errorDomain;
  nsCocoaUtils::GetStringForNSString(error.localizedDescription,
                                     errorDescription);
  nsCocoaUtils::GetStringForNSString(error.domain, errorDomain);
  MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
          ("SSORequestDelegate::didCompleteWithError: domain "
           "'%s' code %ld (%s)",
           NS_ConvertUTF16toUTF8(errorDomain).get(), error.code,
           NS_ConvertUTF16toUTF8(errorDescription).get()));
  if ([error.domain isEqualToString:ASAuthorizationErrorDomain]) {
    switch (error.code) {
      case ASAuthorizationErrorCanceled:
        MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
                ("SSORequestDelegate::didCompleteWithError: Authorization "
                 "error: The user canceled the authorization attempt."));
        break;
      case ASAuthorizationErrorFailed:
        MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
                ("SSORequestDelegate::didCompleteWithError: Authorization "
                 "error: The authorization attempt failed."));
        break;
      case ASAuthorizationErrorInvalidResponse:
        MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
                ("Authorization error: The authorization request received an "
                 "invalid response."));
        break;
      case ASAuthorizationErrorNotHandled:
        MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
                ("SSORequestDelegate::didCompleteWithError: Authorization "
                 "error: The authorization request wasn’t handled."));
        break;
      case ASAuthorizationErrorUnknown:
        MOZ_LOG(
            gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
            ("SSORequestDelegate::didCompleteWithError: Authorization error: "
             "The authorization attempt failed for an unknown reason."));
        break;
      case ASAuthorizationErrorNotInteractive:
        MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
                ("SSORequestDelegate::didCompleteWithError: Authorization "
                 "error: The authorization request isn’t interactive."));
        break;
      default:
        MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
                ("SSORequestDelegate::didCompleteWithError: Authorization "
                 "error: Unhandled error code."));
        break;
    }
  }

  mozilla::glean::network_sso::entra_success.Get("broker_error"_ns).Add(1);
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "SSORequestDelegate::didCompleteWithError", [callback(mCallback)]() {
        MOZ_ASSERT(NS_IsMainThread());
        callback->InvokeCallback();
      }));
}
@end

namespace mozilla {
namespace net {

MicrosoftEntraSSOUtils::MicrosoftEntraSSOUtils(
    nsHttpChannel* aChannel, std::function<void()>&& aResultCallback)
    : mProvider(nullptr),
      mAuthorizationController(nullptr),
      mRequestDelegate(nullptr),
      mChannel(aChannel),
      mResultCallback(std::move(aResultCallback)) {
  MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
          ("MicrosoftEntraSSOUtils::MicrosoftEntraSSOUtils()"));
}

MicrosoftEntraSSOUtils::~MicrosoftEntraSSOUtils() {
  MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
          ("MicrosoftEntraSSOUtils::~MicrosoftEntraSSOUtils()"));
  if (mRequestDelegate) {
    [mRequestDelegate release];
    mRequestDelegate = nil;
  }
  if (mAuthorizationController) {
    [mAuthorizationController release];
    mAuthorizationController = nil;
  }
}

void MicrosoftEntraSSOUtils::AddRequestHeader(const nsACString& aKey,
                                              const nsACString& aValue) {
  mRequestHeaders.InsertOrUpdate(aKey, aValue);
}

// Used to return to nsHttpChannel::ContinuePrepareToConnect after the delegate
// completes its job
void MicrosoftEntraSSOUtils::InvokeCallback() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mChannel,
             "channel needs to be initialized for MicrosoftEntraSSOUtils");

  if (!mRequestHeaders.IsEmpty()) {
    for (auto iter = mRequestHeaders.Iter(); !iter.Done(); iter.Next()) {
      // Passed value will be merged to any existing value.
      mChannel->SetRequestHeader(iter.Key(), iter.Data(), true);
    }
  }

  std::function<void()> callback = std::move(mResultCallback);
  if (callback) {
    callback();
  }
}

bool MicrosoftEntraSSOUtils::AddMicrosoftEntraSSOInternal() {
  MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
          ("MicrosoftEntraSSOUtils::AddMicrosoftEntraSSO start"));
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mChannel,
             "channel needs to be initialized for MicrosoftEntraSSOUtils");

  NSURL* url =
      [NSURL URLWithString:@"https://login.microsoftonline.com/common"];

  mProvider = [ASAuthorizationSingleSignOnProvider
      authorizationProviderWithIdentityProviderURL:url];
  if (!mProvider) {
    return false;
  }

  if (![mProvider canPerformAuthorization]) {
    return false;
  }

  nsCOMPtr<nsIURI> uri;
  mChannel->GetURI(getter_AddRefs(uri));
  if (!url) {
    return false;
  }

  nsAutoCString urispec;
  uri->GetSpec(urispec);
  NSString* urispecNSString = [NSString stringWithUTF8String:urispec.get()];
  MOZ_LOG(gMacOSWebAuthnServiceLog, mozilla::LogLevel::Debug,
          ("MicrosoftEntraSSOUtils::AddMicrosoftEntraSSO [urispec=%s]",
           urispec.get()));

  // Create a controller and initialize it with SSO requests
  ASAuthorizationSingleSignOnRequest* ssoRequest = [mProvider createRequest];
  ssoRequest.requestedOperation = @"get_sso_cookies";
  ssoRequest.userInterfaceEnabled = NO;

  // Set NSURLQueryItems for the MS broker
  NSURLQueryItem* ssoUrl = [NSURLQueryItem queryItemWithName:@"sso_url"
                                                       value:urispecNSString];
  NSURLQueryItem* typesOfHeader =
      [NSURLQueryItem queryItemWithName:@"types_of_header" value:@"0"];
  NSURLQueryItem* brokerKey = [NSURLQueryItem
      queryItemWithName:@"broker_key"
                  value:@"kSiiehqi0sbYWxT2zOmV-rv8B3QRNsUKcU3YPc122121"];
  NSURLQueryItem* protocolVer =
      [NSURLQueryItem queryItemWithName:@"msg_protocol_ver" value:@"4"];
  ssoRequest.authorizationOptions =
      @[ ssoUrl, typesOfHeader, brokerKey, protocolVer ];

  if (!ssoRequest) {
    return false;
  }

  mAuthorizationController = [[ASAuthorizationController alloc]
      initWithAuthorizationRequests:@[ ssoRequest ]];
  if (!mAuthorizationController) {
    return false;
  }

  mRequestDelegate = [[SSORequestDelegate alloc] init];
  [mRequestDelegate setCallback:this];
  mAuthorizationController.delegate = mRequestDelegate;

  [mAuthorizationController performRequests];

  // Return true after acknowledging that the delegate will be called
  return true;
}

API_AVAILABLE(macos(13.3))
nsresult AddMicrosoftEntraSSO(nsHttpChannel* aChannel,
                              std::function<void()>&& aResultCallback) {
  MOZ_ASSERT(XRE_IsParentProcess());

  // The service is used by this function and the delegate, and it should be
  // released when the delegate finishes running. It will remain alive even
  // after AddMicrosoftEntraSSO returns.
  RefPtr<MicrosoftEntraSSOUtils> service =
      new MicrosoftEntraSSOUtils(aChannel, std::move(aResultCallback));

  mozilla::glean::network_sso::total_entra_uses.Add(1);

  if (!service->AddMicrosoftEntraSSOInternal()) {
    mozilla::glean::network_sso::entra_success
        .Get("invalid_controller_setup"_ns)
        .Add(1);
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}
}  // namespace net
}  // namespace mozilla

NS_ASSUME_NONNULL_END
