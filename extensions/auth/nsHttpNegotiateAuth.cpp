/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// HTTP Negotiate Authentication Support Module
//
// Described by IETF Internet draft: draft-brezak-kerberos-http-00.txt
// (formerly draft-brezak-spnego-http-04.txt)
//
// Also described here:
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dnsecure/html/http-sso-1.asp
//

#include <stdlib.h>

#include "nsAuth.h"
#include "nsHttpNegotiateAuth.h"

#include "nsIHttpAuthenticableChannel.h"
#include "nsIAuthModule.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsIProxyInfo.h"
#include "nsIURI.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsNetCID.h"
#include "nsProxyRelease.h"
#include "plbase64.h"
#include "mozilla/Atomics.h"
#include "mozilla/Base64.h"
#include "mozilla/Tokenizer.h"
#include "mozilla/Sprintf.h"
#include "nsIChannel.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "nsIHttpAuthenticatorCallback.h"
#include "nsICancelable.h"
#include "mozilla/net/HttpAuthUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/net/DNS.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_network.h"

#ifdef XP_WIN
#  include "nsITransportSecurityInfo.h"
#  include "nsIX509Cert.h"
#endif

using mozilla::Base64Decode;

//-----------------------------------------------------------------------------

static const char kNegotiate[] = "Negotiate";
static const char kNegotiateAuthTrustedURIs[] =
    "network.negotiate-auth.trusted-uris";
static const char kNegotiateAuthDelegationURIs[] =
    "network.negotiate-auth.delegation-uris";
static const char kNegotiateAuthAllowProxies[] =
    "network.negotiate-auth.allow-proxies";
static const char kNegotiateAuthAllowNonFqdn[] =
    "network.negotiate-auth.allow-non-fqdn";
static const char kSSOinPBmode[] = "network.auth.private-browsing-sso";

mozilla::StaticRefPtr<nsHttpNegotiateAuth> nsHttpNegotiateAuth::gSingleton;

#define kNegotiateLen (sizeof(kNegotiate) - 1)
#define DEFAULT_THREAD_TIMEOUT_MS 30000

// Wraps an nsIAuthModule that has not yet been initialized. Its InitAsync()
// method starts the real module's InitAsync (which may resolve a canonical
// hostname asynchronously), falling back to sync Init if not supported.
// After init, GetNextToken/Wrap/Unwrap delegate to the inner module.
//
// This is created on the main thread and kept as the channel's continuation
// state, but it is addrefed and used by GetNextTokenRunnable on a background
// thread, so it needs threadsafe refcounting.
class NegotiateAuthState final : public nsIAuthModule {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NegotiateAuthState(already_AddRefed<nsIAuthModule> aModule,
                     const nsACString& aServiceName, uint32_t aReqFlags)
      : mModule(aModule), mServiceName(aServiceName), mReqFlags(aReqFlags) {}

  NS_IMETHODIMP Init(const nsACString&, uint32_t, const nsAString&,
                     const nsAString&, const nsAString&) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHODIMP InitAsync(const nsACString&, uint32_t, const nsAString&,
                          const nsAString&, const nsAString&,
                          nsIAuthModuleInitCallback* aCallback) override {
    if (!mInitDone.compareExchange(false, true)) {
      aCallback->OnInitDone(NS_OK);
      return NS_OK;
    }
    nsresult rv = mModule->InitAsync(mServiceName, mReqFlags, u""_ns, u""_ns,
                                     u""_ns, aCallback);
    if (rv == NS_ERROR_NOT_IMPLEMENTED) {
      rv = mModule->Init(mServiceName, mReqFlags, u""_ns, u""_ns, u""_ns);
      aCallback->OnInitDone(rv);
      return NS_OK;
    }
    return rv;
  }

  NS_IMETHODIMP GetNextToken(const void* aInToken, uint32_t aInTokenLen,
                             void** aOutToken,
                             uint32_t* aOutTokenLen) override {
    return mModule->GetNextToken(aInToken, aInTokenLen, aOutToken,
                                 aOutTokenLen);
  }

  NS_IMETHODIMP Wrap(const void* aInToken, uint32_t aInTokenLen,
                     bool aConfidential, void** aOutToken,
                     uint32_t* aOutTokenLen) override {
    return mModule->Wrap(aInToken, aInTokenLen, aConfidential, aOutToken,
                         aOutTokenLen);
  }

  NS_IMETHODIMP Unwrap(const void* aInToken, uint32_t aInTokenLen,
                       void** aOutToken, uint32_t* aOutTokenLen) override {
    return mModule->Unwrap(aInToken, aInTokenLen, aOutToken, aOutTokenLen);
  }

 private:
  ~NegotiateAuthState() = default;

  nsCOMPtr<nsIAuthModule> mModule;
  nsCString mServiceName;
  const uint32_t mReqFlags;
  mozilla::Atomic<bool> mInitDone{false};
};

NS_IMPL_ISUPPORTS(NegotiateAuthState, nsIAuthModule)

//-----------------------------------------------------------------------------

// Return false when the channel comes from a Private browsing window.
static bool TestNotInPBMode(nsIHttpAuthenticableChannel* authChannel,
                            bool proxyAuth) {
  // Proxy should go all the time, it's not considered a privacy leak
  // to send default credentials to a proxy.
  if (proxyAuth) {
    return true;
  }

  nsCOMPtr<nsIChannel> bareChannel = do_QueryInterface(authChannel);
  MOZ_ASSERT(bareChannel);

  if (!NS_UsePrivateBrowsing(bareChannel)) {
    return true;
  }

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefs) {
    bool ssoInPb;
    if (NS_SUCCEEDED(prefs->GetBoolPref(kSSOinPBmode, &ssoInPb)) && ssoInPb) {
      return true;
    }

    // When the "Never remember history" option is set, all channels are
    // set PB mode flag, but here we want to make an exception, users
    // want their credentials go out.
    if (mozilla::StaticPrefs::browser_privatebrowsing_autostart()) {
      return true;
    }
  }

  return false;
}

#ifdef XP_WIN
// Retrieves the DER encoded server certificate, from which the SSPI module
// builds the tls-server-end-point channel binding token that servers with
// Extended Protection enabled require. Must run on the main thread.
//
// Not having a security info object is a valid case rather than an error: it
// happens for plain http, and for an https site reached through a proxy before
// the tunnel has been created.
static void GetServerCertDER(nsIHttpAuthenticableChannel* authChannel,
                             nsTArray<uint8_t>& certDER) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(authChannel);
  if (!channel) {
    return;
  }

  nsCOMPtr<nsITransportSecurityInfo> securityInfo;
  if (NS_FAILED(channel->GetSecurityInfo(getter_AddRefs(securityInfo))) ||
      !securityInfo) {
    return;
  }

  nsCOMPtr<nsIX509Cert> cert;
  if (NS_FAILED(securityInfo->GetServerCert(getter_AddRefs(cert))) || !cert) {
    return;
  }

  if (NS_FAILED(cert->GetRawDER(certDER))) {
    certDER.Clear();
  }
}
#endif

already_AddRefed<nsIHttpAuthenticator> nsHttpNegotiateAuth::GetOrCreate() {
  nsCOMPtr<nsIHttpAuthenticator> authenticator;
  if (gSingleton) {
    authenticator = gSingleton;
  } else {
    gSingleton = new nsHttpNegotiateAuth();
    mozilla::ClearOnShutdown(&gSingleton);
    authenticator = gSingleton;
  }

  return authenticator.forget();
}

NS_IMETHODIMP
nsHttpNegotiateAuth::GetAuthFlags(uint32_t* flags) {
  //
  // Negotiate Auth creds should not be reused across multiple requests.
  // Only perform the negotiation when it is explicitly requested by the
  // server.  Thus, do *NOT* use the "REUSABLE_CREDENTIALS" flag here.
  //
  // CONNECTION_BASED is specified instead of REQUEST_BASED since we need
  // to complete a sequence of transactions with the server over the same
  // connection.
  //
  *flags = CONNECTION_BASED | IDENTITY_IGNORED;
  return NS_OK;
}

//
// Always set *identityInvalid == FALSE here.  This
// will prevent the browser from popping up the authentication
// prompt window.  Because GSSAPI does not have an API
// for fetching initial credentials (ex: A Kerberos TGT),
// there is no correct way to get the users credentials.
//
NS_IMETHODIMP
nsHttpNegotiateAuth::ChallengeReceived(nsIHttpAuthenticableChannel* authChannel,
                                       const nsACString& /*challenge*/,
                                       bool isProxyAuth,
                                       nsISupports** /*sessionState*/,
                                       nsISupports** continuationState,
                                       bool* identityInvalid) {
  *identityInvalid = false;
  if (*continuationState) {
    nsCOMPtr<nsIAuthModule> existing = do_QueryInterface(*continuationState);
    return existing ? NS_OK : NS_ERROR_UNEXPECTED;
  }

  nsresult rv;
  nsCOMPtr<nsIAuthModule> module;

  nsCOMPtr<nsIURI> uri;
  rv = authChannel->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv)) return rv;

  uint32_t req_flags = nsIAuthModule::REQ_DEFAULT;
  nsAutoCString service;

  if (isProxyAuth) {
    if (!TestBoolPref(kNegotiateAuthAllowProxies)) {
      LOG(("nsHttpNegotiateAuth::ChallengeReceived proxy auth blocked\n"));
      return NS_ERROR_ABORT;
    }

    req_flags |= nsIAuthModule::REQ_PROXY_AUTH;
    nsCOMPtr<nsIProxyInfo> proxyInfo;
    authChannel->GetProxyInfo(getter_AddRefs(proxyInfo));
    NS_ENSURE_STATE(proxyInfo);

    proxyInfo->GetHost(service);
  } else {
    bool allowed =
        TestNotInPBMode(authChannel, isProxyAuth) &&
        (TestNonFqdn(uri) || mozilla::net::auth::URIMatchesPrefPattern(
                                 uri, kNegotiateAuthTrustedURIs));
    if (!allowed) {
      LOG(("nsHttpNegotiateAuth::ChallengeReceived URI blocked\n"));
      return NS_ERROR_ABORT;
    }

    bool delegation = mozilla::net::auth::URIMatchesPrefPattern(
        uri, kNegotiateAuthDelegationURIs);
    if (delegation) {
      LOG(("  using REQ_DELEGATE\n"));
      req_flags |= nsIAuthModule::REQ_DELEGATE;
    }

    rv = uri->GetAsciiHost(service);
    if (NS_FAILED(rv)) return rv;
  }

  LOG(("  service = %s\n", service.get()));

  //
  // The correct service name for IIS servers is "HTTP/f.q.d.n", so
  // construct the proper service name for passing to "gss_import_name".
  //
  // TODO: Possibly make this a configurable service name for use
  // with non-standard servers that use stuff like "khttp/f.q.d.n"
  // instead.
  //
  service.InsertLiteral("HTTP@", 0);

  const char* authType = "negotiate-gss";
#ifdef XP_WIN
  if (mozilla::StaticPrefs::network_auth_use_sspi()) {
    authType = "negotiate-sspi";
  }
#endif
  LOG(("  using %s\n", authType));

  MOZ_ALWAYS_TRUE(module = nsIAuthModule::CreateInstance(authType));

  // Don't call Init here -- it is deferred to the background thread
  // (via InitAsync) so that DNS canonical name resolution and credential
  // acquisition don't block the main thread.
  nsCOMPtr<nsIAuthModule> state =
      new NegotiateAuthState(module.forget(), service, req_flags);
  state.forget(continuationState);
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsHttpNegotiateAuth, nsIHttpAuthenticator)

// Shared by GenerateCredentials and the background runnable used by
// GenerateCredentialsAsync. aCertDER is the DER encoded server certificate
// used to build the channel binding token, and may be empty, in which case no
// channel binding is sent.
static nsresult GenerateCredentialsInternal(
    nsIHttpAuthenticableChannel* /*authChannel*/, const nsACString& aChallenge,
    bool /*isProxyAuth*/, const nsAString& /*domain*/,
    const nsAString& /*username*/, const nsAString& /*password*/,
    nsISupports** /*sessionState*/, nsISupports** continuationState,
    const nsTArray<uint8_t>& aCertDER, uint32_t* flags, nsACString& creds);

namespace {

//
// GetNextTokenCompleteEvent
//
// This event is fired on main thread when async call of
// nsHttpNegotiateAuth::GenerateCredentials is finished. During the Run()
// method the nsIHttpAuthenticatorCallback::OnCredsAvailable is called with
// obtained credentials, flags and NS_OK when successful, otherwise
// NS_ERROR_FAILURE is returned as a result of failed operation.
//
class GetNextTokenCompleteEvent final : public nsIRunnable,
                                        public nsICancelable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit GetNextTokenCompleteEvent(nsIHttpAuthenticatorCallback* aCallback)
      : mCallback(aCallback) {}

  nsresult DispatchSuccess(const nsACString& aCreds, uint32_t aFlags,
                           already_AddRefed<nsISupports> aSessionState,
                           already_AddRefed<nsISupports> aContinuationState) {
    // Called from worker thread
    MOZ_ASSERT(!NS_IsMainThread());

    mCreds = aCreds;
    mFlags = aFlags;
    mResult = NS_OK;
    mSessionState = aSessionState;
    mContinuationState = aContinuationState;
    return NS_DispatchToMainThread(this, NS_DISPATCH_NORMAL);
  }

  nsresult DispatchError(already_AddRefed<nsISupports> aSessionState,
                         already_AddRefed<nsISupports> aContinuationState) {
    // Called from worker thread
    MOZ_ASSERT(!NS_IsMainThread());

    mResult = NS_ERROR_FAILURE;
    mSessionState = aSessionState;
    mContinuationState = aContinuationState;
    return NS_DispatchToMainThread(this, NS_DISPATCH_NORMAL);
  }

  NS_IMETHODIMP Run() override {
    // Runs on main thread
    MOZ_ASSERT(NS_IsMainThread());

    if (!mCancelled) {
      nsCOMPtr<nsIHttpAuthenticatorCallback> callback;
      callback.swap(mCallback);
      callback->OnCredsGenerated(mCreds, mFlags, mResult, mSessionState,
                                 mContinuationState);
    }
    return NS_OK;
  }

  NS_IMETHODIMP Cancel(nsresult aReason) override {
    // Supposed to be called from main thread
    MOZ_ASSERT(NS_IsMainThread());

    mCancelled = true;
    nsCOMPtr<nsIHttpAuthenticatorCallback> callback = std::move(mCallback);
    if (callback) {
      callback->OnCredsGenerated(mCreds, mFlags, aReason, nullptr, nullptr);
    }
    return NS_OK;
  }

 private:
  virtual ~GetNextTokenCompleteEvent() = default;

  nsCOMPtr<nsIHttpAuthenticatorCallback> mCallback;
  nsCString mCreds;
  uint32_t mFlags = 0;
  nsresult mResult = NS_OK;
  bool mCancelled = false;
  nsCOMPtr<nsISupports> mSessionState;
  nsCOMPtr<nsISupports> mContinuationState;
};

inline nsISupports* ToSupports(GetNextTokenCompleteEvent* aEvent) {
  return static_cast<nsIRunnable*>(aEvent);
}

NS_IMPL_ISUPPORTS(GetNextTokenCompleteEvent, nsIRunnable, nsICancelable)

//
// GetNextTokenRunnable
//
// This runnable is created by GenerateCredentialsAsync and it runs
// on the background thread pool and calls GenerateCredentials.
//
class GetNextTokenRunnable final : public mozilla::Runnable,
                                   public nsIAuthModuleInitCallback {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  GetNextTokenRunnable(
      nsMainThreadPtrHandle<nsIHttpAuthenticableChannel>& authChannel,
      const nsACString& challenge, bool isProxyAuth, const nsAString& domain,
      const nsAString& username, const nsAString& password,
      nsTArray<uint8_t>&& certDER, nsISupports* sessionState,
      nsISupports* continuationState,
      nsMainThreadPtrHandle<GetNextTokenCompleteEvent>& aCompleteEvent)
      : mozilla::Runnable("GetNextTokenRunnable"),
        mAuthChannel(authChannel),
        mChallenge(challenge),
        mIsProxyAuth(isProxyAuth),
        mDomain(domain),
        mUsername(username),
        mPassword(password),
        mCertDER(std::move(certDER)),
        mSessionState(sessionState),
        mContinuationState(continuationState),
        mCompleteEvent(aCompleteEvent) {}

  NS_IMETHODIMP Run() override {
    MOZ_ASSERT(!NS_IsMainThread());

    nsCOMPtr<nsIAuthModule> module = do_QueryInterface(mContinuationState);
    if (!module) {
      return mCompleteEvent->DispatchError(mSessionState.forget(),
                                           mContinuationState.forget());
    }

    // InitAsync handles the async DNS canonical name resolution for
    // Kerberos/SSPI. For modules that don't need async init (GSSAPI,
    // already-initialized modules), it completes synchronously via
    // the callback. NegotiateAuthState wraps this and falls back to
    // sync Init if InitAsync is not supported.
    nsresult rv = module->InitAsync(""_ns, 0, u""_ns, u""_ns, u""_ns, this);
    if (rv == NS_ERROR_NOT_IMPLEMENTED) {
      return OnInitDone(NS_OK);
    }
    if (NS_FAILED(rv)) {
      return mCompleteEvent->DispatchError(mSessionState.forget(),
                                           mContinuationState.forget());
    }
    return NS_OK;
  }

  NS_IMETHODIMP OnInitDone(nsresult aResult) override {
    if (NS_FAILED(aResult)) {
      return mCompleteEvent->DispatchError(mSessionState.forget(),
                                           mContinuationState.forget());
    }

    nsCString creds;
    uint32_t flags;
    nsresult rv = ObtainCredentialsAndFlags(creds, &flags);

    if (NS_FAILED(rv)) {
      return mCompleteEvent->DispatchError(mSessionState.forget(),
                                           mContinuationState.forget());
    }

    return mCompleteEvent->DispatchSuccess(creds, flags, mSessionState.forget(),
                                           mContinuationState.forget());
  }

 private:
  ~GetNextTokenRunnable() override = default;

  nsresult ObtainCredentialsAndFlags(nsCString& aCreds, uint32_t* aFlags) {
    nsISupports* sessionState = mSessionState;
    nsISupports* continuationState = mContinuationState;
    // The continuationState is for the sake of completeness propagated
    // to the caller (despite it is not changed in any GenerateCredentials
    // implementation).
    //
    // Should any of the session or continuation states change inside
    // this method, they must be threadsafe.
    nsresult rv = GenerateCredentialsInternal(
        mAuthChannel, mChallenge, mIsProxyAuth, mDomain, mUsername, mPassword,
        &sessionState, &continuationState, mCertDER, aFlags, aCreds);
    // Passing session and continuation state this way to not touch
    // referencing of the object that may not be thread safe.
    // Not having a thread safe referencing doesn't mean the object
    // cannot be used on multiple threads (one example is nsAuthSSPI.)
    // This ensures state objects will be destroyed on the main thread
    // when not changed by GenerateCredentials.
    if (mSessionState != sessionState) {
      mSessionState = sessionState;
    }
    if (mContinuationState != continuationState) {
      mContinuationState = continuationState;
    }
    return rv;
  }

  nsMainThreadPtrHandle<nsIHttpAuthenticableChannel> mAuthChannel;
  nsCString mChallenge;
  bool mIsProxyAuth;
  nsString mDomain;
  nsString mUsername;
  nsString mPassword;
  nsTArray<uint8_t> mCertDER;
  nsCOMPtr<nsISupports> mSessionState;
  nsCOMPtr<nsISupports> mContinuationState;
  nsMainThreadPtrHandle<GetNextTokenCompleteEvent> mCompleteEvent;
};

NS_IMPL_ISUPPORTS_INHERITED(GetNextTokenRunnable, mozilla::Runnable,
                            nsIAuthModuleInitCallback)

}  // anonymous namespace

NS_IMETHODIMP
nsHttpNegotiateAuth::GenerateCredentialsAsync(
    nsIHttpAuthenticableChannel* authChannel,
    nsIHttpAuthenticatorCallback* aCallback, const nsACString& challenge,
    bool isProxyAuth, const nsAString& domain, const nsAString& username,
    const nsAString& password, nsISupports* sessionState,
    nsISupports* continuationState, nsICancelable** aCancelable) {
  NS_ENSURE_ARG(aCallback);
  NS_ENSURE_ARG_POINTER(aCancelable);

  nsTArray<uint8_t> certDER;
#ifdef XP_WIN
  // The certificate has to be collected here because GenerateCredentials runs
  // on a background thread. Only a challenge with no token of its own starts a
  // sequence, and that is the only call nsAuthSSPI accepts a certificate on.
  // Proxy auth is excluded because the channel's security info describes the
  // origin server rather than the proxy.
  if (!isProxyAuth && challenge.Length() <= kNegotiateLen &&
      mozilla::StaticPrefs::network_auth_negotiate_channel_binding() &&
      mozilla::StaticPrefs::network_auth_use_sspi()) {
    GetServerCertDER(authChannel, certDER);
  }
#endif

  nsMainThreadPtrHandle<nsIHttpAuthenticableChannel> handle(
      new nsMainThreadPtrHolder<nsIHttpAuthenticableChannel>(
          "nsIHttpAuthenticableChannel", authChannel, false));
  nsMainThreadPtrHandle<GetNextTokenCompleteEvent> cancelEvent(
      new nsMainThreadPtrHolder<GetNextTokenCompleteEvent>(
          "GetNextTokenRunnable::mCompleteEvent",
          new GetNextTokenCompleteEvent(aCallback), false));
  nsCOMPtr<nsIRunnable> getNextTokenRunnable = new GetNextTokenRunnable(
      handle, challenge, isProxyAuth, domain, username, password,
      std::move(certDER), sessionState, continuationState, cancelEvent);

  nsresult rv = NS_DispatchBackgroundTask(
      getNextTokenRunnable, nsIEventTarget::DISPATCH_EVENT_MAY_BLOCK);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<GetNextTokenCompleteEvent> cancelable(cancelEvent.get());
  cancelable.forget(aCancelable);
  return NS_OK;
}

//
// GenerateCredentials
//
// This routine is responsible for creating the correct authentication
// blob to pass to the server that requested "Negotiate" authentication.
//
NS_IMETHODIMP
nsHttpNegotiateAuth::GenerateCredentials(
    nsIHttpAuthenticableChannel* authChannel, const nsACString& aChallenge,
    bool isProxyAuth, const nsAString& domain, const nsAString& username,
    const nsAString& password, nsISupports** sessionState,
    nsISupports** continuationState, uint32_t* flags, nsACString& creds) {
  // Only reachable when GenerateCredentialsAsync fails to dispatch, which in
  // practice means shutdown. No certificate is passed, so this path sends no
  // channel binding; the sequence is not going to complete anyway.
  return GenerateCredentialsInternal(
      authChannel, aChallenge, isProxyAuth, domain, username, password,
      sessionState, continuationState, nsTArray<uint8_t>(), flags, creds);
}

static nsresult GenerateCredentialsInternal(
    nsIHttpAuthenticableChannel* /*authChannel*/, const nsACString& aChallenge,
    bool /*isProxyAuth*/, const nsAString& /*domain*/,
    const nsAString& /*username*/, const nsAString& /*password*/,
    nsISupports** /*sessionState*/, nsISupports** continuationState,
    const nsTArray<uint8_t>& aCertDER, uint32_t* flags, nsACString& creds) {
  // ChallengeReceived must have been called previously.
  nsCOMPtr<nsIAuthModule> module = do_QueryInterface(*continuationState);
  NS_ENSURE_TRUE(module, NS_ERROR_NOT_INITIALIZED);

  *flags = nsIHttpAuthenticator::USING_INTERNAL_IDENTITY;

  LOG(("nsHttpNegotiateAuth::GenerateCredentials() [challenge=%s]\n",
       PromiseFlatCString(aChallenge).get()));

#ifdef DEBUG
  bool isGssapiAuth = StringBeginsWith(aChallenge, "Negotiate"_ns,
                                       nsCaseInsensitiveCStringComparator);
  NS_ASSERTION(isGssapiAuth, "Unexpected challenge");
#endif

  //
  // If the "Negotiate:" header had some data associated with it,
  // that data should be used as the input to this call.  This may
  // be a continuation of an earlier call because GSSAPI authentication
  // often takes multiple round-trips to complete depending on the
  // context flags given.  We want to use MUTUAL_AUTHENTICATION which
  // generally *does* require multiple round-trips.  Don't assume
  // auth can be completed in just 1 call.
  //

  nsAutoCString inToken;
  if (aChallenge.Length() > kNegotiateLen) {
    nsDependentCSubstring challenge(aChallenge, kNegotiateLen);
    uint32_t startPos = 0;
    while (startPos < challenge.Length() && challenge[startPos] == ' ') {
      startPos++;
    }
    if (startPos == challenge.Length()) {
      return NS_ERROR_UNEXPECTED;
    }

    // strip off any padding (see bug 230351)
    uint32_t len = challenge.Length();
    while (len > startPos && challenge[len - 1] == '=') {
      len--;
    }

    //
    // Decode the response that followed the "Negotiate" token
    //
    (void)Base64Decode(
        nsDependentCSubstring(challenge, startPos, len - startPos), inToken);
  }

  void* outToken = nullptr;
  uint32_t outTokenLen = 0;
  nsresult rv;
  if (inToken.IsEmpty() && !aCertDER.IsEmpty()) {
    // On the first call of a sequence the SSPI module expects the server
    // certificate in place of the input token, so that it can compute the
    // channel binding token. See nsAuthSSPI::GetNextToken.
    rv = module->GetNextToken(aCertDER.Elements(), aCertDER.Length(), &outToken,
                              &outTokenLen);
  } else {
    rv = module->GetNextToken(inToken.get(), inToken.Length(), &outToken,
                              &outTokenLen);
  }
  if (NS_FAILED(rv)) {
    if (outToken) {
      // Technically if the call fails we shouln't have allocated, but
      // Coverity doesn't know that.
      free(outToken);
    }
    return rv;
  }

  if (outTokenLen == 0) {
    LOG(("  No output token to send, exiting"));
    return NS_ERROR_FAILURE;
  }

  //
  // base64 encode the output token.
  //
  nsAutoCString encodedToken;
  rv = mozilla::Base64Encode(
      nsDependentCSubstring((char*)outToken, outTokenLen), encodedToken);
  free(outToken);
  if (NS_FAILED(rv)) {
    return rv;
  }

  LOG(("  Sending a token of length %d\n", outTokenLen));

  creds = nsPrintfCString("%s %s", kNegotiate, encodedToken.get());
  return rv;
}

bool nsHttpNegotiateAuth::TestBoolPref(const char* pref) {
  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (!prefs) return false;

  bool val;
  nsresult rv = prefs->GetBoolPref(pref, &val);
  if (NS_FAILED(rv)) return false;

  return val;
}

bool nsHttpNegotiateAuth::TestNonFqdn(nsIURI* uri) {
  nsAutoCString host;

  if (!TestBoolPref(kNegotiateAuthAllowNonFqdn)) {
    return false;
  }

  if (NS_FAILED(uri->GetAsciiHost(host))) {
    return false;
  }

  // return true if host does not contain a dot and is not an ip address
  return !host.IsEmpty() && !host.Contains('.') &&
         !mozilla::net::HostIsIPLiteral(host);
}
