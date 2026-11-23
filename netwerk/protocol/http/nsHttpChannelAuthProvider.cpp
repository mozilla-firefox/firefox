/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "nsHttpChannelAuthProvider.h"

#include "HttpLog.h"
#include "MockHttpAuth.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/Preferences.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/Tokenizer.h"
#include "netCore.h"
#include "nsAuthInformationHolder.h"
#include "nsCRT.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsEscape.h"
#include "nsHttp.h"
#include "nsHttpBasicAuth.h"
#include "nsHttpDigestAuth.h"
#include "nsHttpHandler.h"
#include "nsIAuthPrompt2.h"
#include "nsIAuthPromptProvider.h"
#include "nsIHttpAuthenticableChannel.h"
#include "nsIHttpAuthenticator.h"
#include "nsIHttpChannelInternal.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPromptService.h"
#include "nsIStringBundle.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#ifdef MOZ_AUTH_EXTENSION
#  include "nsHttpNegotiateAuth.h"
#endif
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_prompts.h"
#include "nsHttpNTLMAuth.h"
#include "nsIProxiedChannel.h"
#include "nsIProxyInfo.h"
#include "nsIURL.h"
#include "nsServiceManagerUtils.h"

namespace mozilla::net {

#define SUBRESOURCE_AUTH_DIALOG_DISALLOW_ALL 0
#define SUBRESOURCE_AUTH_DIALOG_DISALLOW_CROSS_ORIGIN 1
#define SUBRESOURCE_AUTH_DIALOG_ALLOW_ALL 2

#define MAX_DISPLAYED_USER_LENGTH 64
#define MAX_DISPLAYED_HOST_LENGTH 64

static void GetOriginAttributesSuffix(nsIChannel* aChan, nsACString& aSuffix) {
  OriginAttributes oa;

  // Deliberately ignoring the result and going with defaults
  if (aChan) {
    StoragePrincipalHelper::GetOriginAttributesForNetworkState(aChan, oa);
  }

  oa.CreateSuffix(aSuffix);
}

nsHttpChannelAuthProvider::nsHttpChannelAuthProvider()
    : mProxyAuth(false),
      mTriedProxyAuth(false),
      mTriedHostAuth(false),
      mCrossOrigin(false),
      mConnectionBased(false),
      mHttpHandler(gHttpHandler) {}

nsHttpChannelAuthProvider::~nsHttpChannelAuthProvider() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mAuthChannel, "Disconnect wasn't called");
}

NS_IMETHODIMP
nsHttpChannelAuthProvider::Init(nsIHttpAuthenticableChannel* channel) {
  MOZ_ASSERT(channel, "channel expected!");

  mAuthChannel = channel;

  nsresult rv = mAuthChannel->GetURI(getter_AddRefs(mURI));
  if (NS_FAILED(rv)) return rv;

  mUsingSSL = mURI->SchemeIs("https");

  nsCOMPtr<nsIProxiedChannel> proxied(channel);
  if (proxied) {
    nsCOMPtr<nsIProxyInfo> pi;
    rv = proxied->GetProxyInfo(getter_AddRefs(pi));
    if (NS_FAILED(rv)) return rv;

    if (pi) {
      nsAutoCString proxyType;
      rv = pi->GetType(proxyType);
      if (NS_FAILED(rv)) return rv;

      mProxyUsingSSL = proxyType.EqualsLiteral("https");
    }
  }

  rv = mURI->GetAsciiHost(mHost);
  if (NS_FAILED(rv)) return rv;

  // reject the URL if it doesn't specify a host
  if (mHost.IsEmpty()) return NS_ERROR_MALFORMED_URI;

  rv = mURI->GetPort(&mPort);
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIChannel> bareChannel = do_QueryInterface(channel);
  mIsPrivate = NS_UsePrivateBrowsing(bareChannel);

  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannelAuthProvider::ProcessAuthentication(uint32_t httpStatus,
                                                 bool SSLConnectFailed) {
  LOG(
      ("nsHttpChannelAuthProvider::ProcessAuthentication "
       "[this=%p channel=%p code=%u SSLConnectFailed=%d]\n",
       this, mAuthChannel, httpStatus, SSLConnectFailed));

  MOZ_ASSERT(mAuthChannel, "Channel not initialized");

  nsCOMPtr<nsIProxyInfo> proxyInfo;
  nsresult rv = mAuthChannel->GetProxyInfo(getter_AddRefs(proxyInfo));
  if (NS_FAILED(rv)) return rv;
  if (proxyInfo) {
    mProxyInfo = do_QueryInterface(proxyInfo);
    if (!mProxyInfo) return NS_ERROR_NO_INTERFACE;
  }

  nsAutoCString challenges;
  mProxyAuth = (httpStatus == 407);

  rv = PrepareForAuthentication(mProxyAuth);
  if (NS_FAILED(rv)) return rv;

  if (mProxyAuth) {
    // only allow a proxy challenge if we have a proxy server configured.
    // otherwise, we could inadvertently expose the user's proxy
    // credentials to an origin server.  We could attempt to proceed as
    // if we had received a 401 from the server, but why risk flirting
    // with trouble?  IE similarly rejects 407s when a proxy server is
    // not configured, so there's no reason not to do the same.
    if (!UsingHttpProxy()) {
      LOG(("rejecting 407 when proxy server not configured!\n"));
      return NS_ERROR_UNEXPECTED;
    }
    if (UsingSSL() && !SSLConnectFailed) {
      // we need to verify that this challenge came from the proxy
      // server itself, and not some server on the other side of the
      // SSL tunnel.
      LOG(("rejecting 407 from origin server!\n"));
      return NS_ERROR_UNEXPECTED;
    }
    rv = mAuthChannel->GetProxyChallenges(challenges);
  } else {
    rv = mAuthChannel->GetWWWChallenges(challenges);
  }
  if (NS_FAILED(rv)) return rv;

  nsAutoCString creds;
  rv = GetCredentials(challenges, mProxyAuth, creds);
  if (rv == NS_ERROR_IN_PROGRESS || rv == NS_ERROR_BASIC_HTTP_AUTH_DISABLED ||
      rv == NS_ERROR_HTTP_AUTH_SCHEME_DISABLED) {
    return rv;
  }

  if (NS_FAILED(rv)) {
    LOG(("unable to authenticate\n"));
  } else {
    // set the authentication credentials
    if (mProxyAuth) {
      rv = mAuthChannel->SetProxyCredentials(creds);
    } else {
      rv = mAuthChannel->SetWWWCredentials(creds);
    }
  }
  return rv;
}

NS_IMETHODIMP
nsHttpChannelAuthProvider::AddAuthorizationHeaders(
    bool aDontUseCachedWWWCreds) {
  LOG(
      ("nsHttpChannelAuthProvider::AddAuthorizationHeaders? "
       "[this=%p channel=%p]\n",
       this, mAuthChannel));

  MOZ_ASSERT(mAuthChannel, "Channel not initialized");

  nsCOMPtr<nsIProxyInfo> proxyInfo;
  nsresult rv = mAuthChannel->GetProxyInfo(getter_AddRefs(proxyInfo));
  if (NS_FAILED(rv)) return rv;
  if (proxyInfo) {
    mProxyInfo = do_QueryInterface(proxyInfo);
    if (!mProxyInfo) return NS_ERROR_NO_INTERFACE;
  }

  uint32_t loadFlags;
  rv = mAuthChannel->GetLoadFlags(&loadFlags);
  if (NS_FAILED(rv)) return rv;

  // this getter never fails
  nsHttpAuthCache* authCache = gHttpHandler->AuthCache(mIsPrivate);

  // check if proxy credentials should be sent
  if (!ProxyHost().IsEmpty() && UsingHttpProxy()) {
    SetAuthorizationHeader(authCache, nsHttp::Proxy_Authorization, "http"_ns,
                           ProxyHost(), ProxyPort(),
                           ""_ns,  // proxy has no path
                           mProxyIdent);
  }

  if (loadFlags & nsIRequest::LOAD_ANONYMOUS) {
    LOG(("Skipping Authorization header for anonymous load\n"));
    return NS_OK;
  }

  if (aDontUseCachedWWWCreds) {
    LOG(
        ("Authorization header already present:"
         " skipping adding auth header from cache\n"));
    return NS_OK;
  }

  // check if server credentials should be sent
  nsAutoCString path, scheme;
  if (NS_SUCCEEDED(GetCurrentPath(path)) &&
      NS_SUCCEEDED(mURI->GetScheme(scheme))) {
    SetAuthorizationHeader(authCache, nsHttp::Authorization, scheme, Host(),
                           Port(), path, mIdent);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannelAuthProvider::Cancel(nsresult status) {
  MOZ_ASSERT(mAuthChannel, "Channel not initialized");

  if (mAsyncPromptAuthCancelable) {
    mAsyncPromptAuthCancelable->Cancel(status);
    mAsyncPromptAuthCancelable = nullptr;
  }

  if (mGenerateCredentialsCancelable) {
    mGenerateCredentialsCancelable->Cancel(status);
    mGenerateCredentialsCancelable = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannelAuthProvider::Disconnect(nsresult status) {
  mAuthChannel = nullptr;

  if (mAsyncPromptAuthCancelable) {
    mAsyncPromptAuthCancelable->Cancel(status);
    mAsyncPromptAuthCancelable = nullptr;
  }

  if (mGenerateCredentialsCancelable) {
    mGenerateCredentialsCancelable->Cancel(status);
    mGenerateCredentialsCancelable = nullptr;
  }

  NS_IF_RELEASE(mProxyAuthContinuationState);
  NS_IF_RELEASE(mAuthContinuationState);

  return NS_OK;
}

// helper function for getting an auth prompt from an interface requestor
static void GetAuthPrompt(nsIInterfaceRequestor* ifreq, bool proxyAuth,
                          nsIAuthPrompt2** result) {
  if (!ifreq) return;

  uint32_t promptReason;
  if (proxyAuth) {
    promptReason = nsIAuthPromptProvider::PROMPT_PROXY;
  } else {
    promptReason = nsIAuthPromptProvider::PROMPT_NORMAL;
  }

  nsCOMPtr<nsIAuthPromptProvider> promptProvider = do_GetInterface(ifreq);
  if (promptProvider) {
    promptProvider->GetAuthPrompt(promptReason, NS_GET_IID(nsIAuthPrompt2),
                                  reinterpret_cast<void**>(result));
  } else {
    NS_QueryAuthPrompt2(ifreq, result);
  }
}

// generate credentials for the given challenge, and update the auth cache.
nsresult nsHttpChannelAuthProvider::GenCredsAndSetEntry(
    nsIHttpAuthenticator* auth, bool proxyAuth, const nsACString& scheme,
    const nsACString& host, int32_t port, const nsACString& directory,
    const nsACString& realm, const nsACString& challenge,
    const nsHttpAuthIdentity& ident, nsCOMPtr<nsISupports>& sessionState,
    nsACString& result) {
  nsresult rv;
  nsISupports* ss = sessionState;

  // set informations that depend on whether
  // we're authenticating against a proxy
  // or a webserver
  nsISupports** continuationState;

  if (proxyAuth) {
    continuationState = &mProxyAuthContinuationState;
  } else {
    continuationState = &mAuthContinuationState;
  }

  rv = auth->GenerateCredentialsAsync(
      mAuthChannel, this, challenge, proxyAuth, ident.Domain(), ident.User(),
      ident.Password(), ss, *continuationState,
      getter_AddRefs(mGenerateCredentialsCancelable));
  if (NS_SUCCEEDED(rv)) {
    // Calling generate credentials async, results will be dispatched to the
    // main thread by calling OnCredsGenerated method
    return NS_ERROR_IN_PROGRESS;
  }

  uint32_t generateFlags;
  rv = auth->GenerateCredentials(
      mAuthChannel, challenge, proxyAuth, ident.Domain(), ident.User(),
      ident.Password(), &ss, &*continuationState, &generateFlags, result);

  sessionState.swap(ss);
  if (NS_FAILED(rv)) return rv;

  // don't log this in release build since it could contain sensitive info.
#ifdef DEBUG
  LOG(("generated creds: %s\n", PromiseFlatCString(result).get()));
#endif

  return UpdateCache(auth, scheme, host, port, directory, realm, challenge,
                     ident, result, generateFlags, sessionState, proxyAuth);
}

nsresult nsHttpChannelAuthProvider::UpdateCache(
    nsIHttpAuthenticator* auth, const nsACString& scheme,
    const nsACString& host, int32_t port, const nsACString& directory,
    const nsACString& realm, const nsACString& challenge,
    const nsHttpAuthIdentity& ident, const nsACString& creds,
    uint32_t generateFlags, nsISupports* sessionState, bool aProxyAuth) {
  nsresult rv;

  uint32_t authFlags;
  rv = auth->GetAuthFlags(&authFlags);
  if (NS_FAILED(rv)) return rv;

  // find out if this authenticator allows reuse of credentials and/or
  // challenge.
  bool saveCreds =
      0 != (authFlags & nsIHttpAuthenticator::REUSABLE_CREDENTIALS);
  bool saveChallenge =
      0 != (authFlags & nsIHttpAuthenticator::REUSABLE_CHALLENGE);

  bool saveIdentity =
      0 == (generateFlags & nsIHttpAuthenticator::USING_INTERNAL_IDENTITY);

  // this getter never fails
  nsHttpAuthCache* authCache = gHttpHandler->AuthCache(mIsPrivate);

  nsAutoCString suffix;
  if (!aProxyAuth) {
    // We don't isolate proxy credentials cache entries with the origin suffix
    // as it would only annoy users with authentication dialogs popping up.
    nsCOMPtr<nsIChannel> chan = do_QueryInterface(mAuthChannel);
    GetOriginAttributesSuffix(chan, suffix);
  }

  // create a cache entry.  we do this even though we don't yet know that
  // these credentials are valid b/c we need to avoid prompting the user
  // more than once in case the credentials are valid.
  //
  // if the credentials are not reusable, then we don't bother sticking
  // them in the auth cache.
  rv = authCache->SetAuthEntry(scheme, host, port, directory, realm,
                               saveCreds ? creds : ""_ns,
                               saveChallenge ? challenge : ""_ns, suffix,
                               saveIdentity ? &ident : nullptr, sessionState);
  return rv;
}

NS_IMETHODIMP nsHttpChannelAuthProvider::ClearProxyIdent() {
  LOG(("nsHttpChannelAuthProvider::ClearProxyIdent [this=%p]\n", this));

  mProxyIdent.Clear();
  return NS_OK;
}

nsresult nsHttpChannelAuthProvider::PrepareForAuthentication(bool proxyAuth) {
  LOG(
      ("nsHttpChannelAuthProvider::PrepareForAuthentication "
       "[this=%p channel=%p]\n",
       this, mAuthChannel));

  if (!proxyAuth) {
    // reset the current proxy continuation state because our last
    // authentication attempt was completed successfully.
    NS_IF_RELEASE(mProxyAuthContinuationState);
    LOG(("  proxy continuation state has been reset"));
  }

  if (!UsingHttpProxy() || mProxyAuthType.IsEmpty()) return NS_OK;

  // We need to remove any Proxy_Authorization header left over from a
  // non-request based authentication handshake (e.g., for NTLM auth).

  nsresult rv;
  nsCOMPtr<nsIHttpAuthenticator> precedingAuth;
  nsCString proxyAuthType;
  rv = GetAuthenticator(mProxyAuthType, proxyAuthType,
                        getter_AddRefs(precedingAuth));
  if (NS_FAILED(rv)) return rv;

  uint32_t precedingAuthFlags;
  rv = precedingAuth->GetAuthFlags(&precedingAuthFlags);
  if (NS_FAILED(rv)) return rv;

  if (!(precedingAuthFlags & nsIHttpAuthenticator::REQUEST_BASED)) {
    nsAutoCString challenges;
    rv = mAuthChannel->GetProxyChallenges(challenges);
    if (NS_FAILED(rv)) {
      // delete the proxy authorization header because we weren't
      // asked to authenticate
      rv = mAuthChannel->SetProxyCredentials(""_ns);
      if (NS_FAILED(rv)) return rv;
      LOG(("  cleared proxy authorization header"));
    }
  }

  return NS_OK;
}

class MOZ_STACK_CLASS ChallengeParser final : Tokenizer {
 public:
  explicit ChallengeParser(const nsACString& aChallenges)
      : Tokenizer(aChallenges, nullptr, "") {
    Record();
  }

  Maybe<nsDependentCSubstring> GetNext() {
    Token t;
    nsDependentCSubstring result;

    bool inQuote = false;

    while (Next(t)) {
      if (t.Type() == TOKEN_EOL) {
        Claim(result, ClaimInclusion::EXCLUDE_LAST);
        SkipWhites(WhiteSkipping::INCLUDE_NEW_LINE);
        Record();
        inQuote = false;
        if (!result.IsEmpty()) {
          return Some(result);
        }
      } else if (t.Equals(Token::Char(',')) && !inQuote) {
        // Sometimes we get multiple challenges separated by a comma.
        // This is not great, as it's slightly ambiguous. We check if something
        // is a new challenge by matching agains <param_name> =
        // If the , isn't followed by a word and = then most likely
        // it is the name of an authType.

        const char* prevCursorPos = mCursor;
        const char* prevRollbackPos = mRollback;

        auto hasWordAndEqual = [&]() {
          SkipWhites();
          nsDependentCSubstring word;
          if (!ReadWord(word)) {
            return false;
          }
          SkipWhites();
          return Check(Token::Char('='));
        };
        if (!hasWordAndEqual()) {
          // This is not a parameter. It means the `,` character starts a
          // different challenge.
          // We'll revert the cursor and return the contents so far.
          mCursor = prevCursorPos;
          mRollback = prevRollbackPos;
          Claim(result, ClaimInclusion::EXCLUDE_LAST);
          SkipWhites();
          Record();
          if (!result.IsEmpty()) {
            return Some(result);
          }
        }
      } else if (t.Equals(Token::Char('"'))) {
        inQuote = !inQuote;
      }
    }

    Claim(result, Tokenizer::ClaimInclusion::INCLUDE_LAST);
    SkipWhites();
    Record();
    if (!result.IsEmpty()) {
      return Some(result);
    }
    return Nothing{};
  }
};

enum ChallengeRank {
  Unknown = 0,
  Basic = 1,
  Digest = 2,
  NTLM = 3,
  Negotiate = 4,
};

ChallengeRank Rank(const nsACString& aChallenge) {
  if (StringBeginsWith(aChallenge, "Negotiate"_ns,
                       nsCaseInsensitiveCStringComparator)) {
    return ChallengeRank::Negotiate;
  }

  if (StringBeginsWith(aChallenge, "NTLM"_ns,
                       nsCaseInsensitiveCStringComparator)) {
    return ChallengeRank::NTLM;
  }

  if (StringBeginsWith(aChallenge, "Digest"_ns,
                       nsCaseInsensitiveCStringComparator)) {
    return ChallengeRank::Digest;
  }

  if (StringBeginsWith(aChallenge, "Basic"_ns,
                       nsCaseInsensitiveCStringComparator)) {
    return ChallengeRank::Basic;
  }

  return ChallengeRank::Unknown;
}

// Parses the value of network.http.auth.disabled-schemes into a bitmask of
// disabled ChallengeRank values, so filtering a whole list of challenges
// only pays for one pass over the pref value instead of one per challenge.
static uint8_t ParseDisabledAuthSchemes(const nsACString& aDisabledSchemes) {
  uint8_t disabled = 0;
  for (const auto& token :
       nsCCharSeparatedTokenizer(aDisabledSchemes, ',').ToRange()) {
    if (token.LowerCaseEqualsASCII("basic")) {
      disabled |= 1 << ChallengeRank::Basic;
    } else if (token.LowerCaseEqualsASCII("digest")) {
      disabled |= 1 << ChallengeRank::Digest;
    } else if (token.LowerCaseEqualsASCII("ntlm")) {
      disabled |= 1 << ChallengeRank::NTLM;
    } else if (token.LowerCaseEqualsASCII("negotiate")) {
      disabled |= 1 << ChallengeRank::Negotiate;
    }
  }
  return disabled;
}

nsresult nsHttpChannelAuthProvider::GetCredentials(
    const nsACString& aChallenges, bool proxyAuth, nsCString& creds) {
  LOG(("nsHttpChannelAuthProvider::GetCredentials"));
  nsAutoCString challenges(aChallenges);

  using AuthChallenge = struct AuthChallenge {
    nsDependentCSubstring challenge;
    uint16_t algorithm = 0;
    ChallengeRank rank = ChallengeRank::Unknown;

    void operator=(const AuthChallenge& aOther) {
      challenge.Rebind(aOther.challenge, 0);
      algorithm = aOther.algorithm;
      rank = aOther.rank;
    }
  };

  nsTArray<AuthChallenge> cc;

  ChallengeParser p(challenges);
  while (true) {
    auto next = p.GetNext();
    if (next.isNothing()) {
      break;
    }
    AuthChallenge ac{next.ref(), 0};
    nsAutoCString realm, domain, nonce, opaque;
    bool stale = false;
    uint16_t qop = 0;
    ac.rank = Rank(ac.challenge);
    if (StringBeginsWith(ac.challenge, "Digest"_ns,
                         nsCaseInsensitiveCStringComparator)) {
      (void)nsHttpDigestAuth::ParseChallenge(ac.challenge, realm, domain, nonce,
                                             opaque, &stale, &ac.algorithm,
                                             &qop);
    }
    cc.AppendElement(std::move(ac));
  }

  nsAutoCString disabledSchemes;
  Preferences::GetCString("network.http.auth.disabled-schemes",
                          disabledSchemes);
  if (!disabledSchemes.IsEmpty() && !cc.IsEmpty()) {
    uint8_t disabledMask = ParseDisabledAuthSchemes(disabledSchemes);
    cc.RemoveElementsBy([disabledMask](const AuthChallenge& c) {
      return disabledMask & (1 << c.rank);
    });
    if (cc.IsEmpty()) {
      return NS_ERROR_HTTP_AUTH_SCHEME_DISABLED;
    }
  }

  // Returns true if an authorization is in progress
  auto authInProgress = [&]() -> bool {
    return proxyAuth ? mProxyAuthContinuationState : mAuthContinuationState;
  };

  // We shouldn't sort if authorization is already in progress
  // otherwise we might end up picking the wrong one. See bug 1805666
  if (!authInProgress() ||
      StaticPrefs::network_auth_sort_challenge_in_progress()) {
    cc.StableSort([](const AuthChallenge& lhs, const AuthChallenge& rhs) {
      // Different auth types
      if (lhs.rank != rhs.rank) {
        return lhs.rank < rhs.rank ? 1 : -1;
      }

      // If they're the same auth type, and not a Digest, then we treat them
      // as equal (don't reorder them).
      if (lhs.rank != ChallengeRank::Digest) {
        return 0;
      }

      if (lhs.algorithm == rhs.algorithm) {
        return 0;
      }

      return lhs.algorithm < rhs.algorithm ? 1 : -1;
    });
  }

  nsAutoCString scheme;

  // If a preference to enable basic HTTP Auth is unset and the scheme is HTTP,
  // check if Basic is the sole available authentication challenge.
  if (NS_SUCCEEDED(mURI->GetScheme(scheme)) && scheme == "http"_ns &&
      !StaticPrefs::network_http_basic_http_auth_enabled() &&
      cc[0].rank == ChallengeRank::Basic) {
    // HTTP Auth and "Basic" is the sole available authentication.
    return NS_ERROR_BASIC_HTTP_AUTH_DISABLED;
  }

  nsCOMPtr<nsIHttpAuthenticator> auth;
  nsCString authType;  // force heap allocation to enable string sharing since
                       // we'll be assigning this value into mAuthType.

  // set informations that depend on whether we're authenticating against a
  // proxy or a webserver
  nsISupports** currentContinuationState;
  nsCString* currentAuthType;

  if (proxyAuth) {
    currentContinuationState = &mProxyAuthContinuationState;
    currentAuthType = &mProxyAuthType;
  } else {
    currentContinuationState = &mAuthContinuationState;
    currentAuthType = &mAuthType;
  }

  nsresult rv = NS_ERROR_NOT_AVAILABLE;
  bool gotCreds = false;

  // figure out which challenge we can handle and which authenticator to use.
  for (size_t i = 0; i < cc.Length(); i++) {
    rv = GetAuthenticator(cc[i].challenge, authType, getter_AddRefs(auth));
    LOG(("trying auth for %s", authType.get()));
    if (NS_SUCCEEDED(rv)) {
      //
      // if we've already selected an auth type from a previous challenge
      // received while processing this channel, then skip others until
      // we find a challenge corresponding to the previously tried auth
      // type.
      //
      if (!currentAuthType->IsEmpty() && authType != *currentAuthType) continue;

      //
      // we allow the routines to run all the way through before we
      // decide if they are valid.
      //
      // we don't worry about the auth cache being altered because that
      // would have been the last step, and if the error is from updating
      // the authcache it wasn't really altered anyway. -CTN
      //
      // at this point the code is really only useful for client side
      // errors (it will not automatically fail over to do a different
      // auth type if the server keeps rejecting what is being sent, even
      // if a particular auth method only knows 1 thing, like a
      // non-identity based authentication method)
      //
      rv = GetCredentialsForChallenge(cc[i].challenge, authType, proxyAuth,
                                      auth, creds);
      if (NS_SUCCEEDED(rv)) {
        gotCreds = true;
        *currentAuthType = authType;

        break;
      }
      if (rv == NS_ERROR_IN_PROGRESS) {
        // authentication prompt has been invoked and result is
        // expected asynchronously, save current challenge being
        // processed and all remaining challenges to use later in
        // OnAuthAvailable and now immediately return
        *currentAuthType = authType;
        mCurrentChallenge = cc[i].challenge;
        // imperfect; does not save server-side preference ordering.
        // instead, continues with remaining string as provided by client
        mRemainingChallenges.Truncate();
        while (i + 1 < cc.Length()) {
          i++;
          mRemainingChallenges.Append(cc[i].challenge);
          mRemainingChallenges.Append("\n"_ns);
        }
        return rv;
      }

      // reset the auth type and continuation state
      NS_IF_RELEASE(*currentContinuationState);
      currentAuthType->Truncate();
    }
  }

  if (!gotCreds && !currentAuthType->IsEmpty()) {
    // looks like we never found the auth type we were looking for.
    // reset the auth type and continuation state, and try again.
    currentAuthType->Truncate();
    NS_IF_RELEASE(*currentContinuationState);

    rv = GetCredentials(challenges, proxyAuth, creds);
  }

  return rv;
}

nsresult nsHttpChannelAuthProvider::GetAuthorizationMembers(
    bool proxyAuth, nsACString& scheme, nsCString& host, int32_t& port,
    nsACString& path, nsHttpAuthIdentity*& ident,
    nsISupports**& continuationState) {
  if (proxyAuth) {
    MOZ_ASSERT(UsingHttpProxy(),
               "proxyAuth is true, but no HTTP proxy is configured!");

    host = ProxyHost();
    port = ProxyPort();
    ident = &mProxyIdent;
    scheme.AssignLiteral("http");

    continuationState = &mProxyAuthContinuationState;
  } else {
    host = Host();
    port = Port();
    ident = &mIdent;

    nsresult rv;
    rv = GetCurrentPath(path);
    if (NS_FAILED(rv)) return rv;

    rv = mURI->GetScheme(scheme);
    if (NS_FAILED(rv)) return rv;

    continuationState = &mAuthContinuationState;
  }

  return NS_OK;
}

nsresult nsHttpChannelAuthProvider::GetCredentialsForChallenge(
    const nsACString& aChallenge, const nsACString& aAuthType, bool proxyAuth,
    nsIHttpAuthenticator* auth, nsCString& creds) {
  LOG(
      ("nsHttpChannelAuthProvider::GetCredentialsForChallenge "
       "[this=%p channel=%p proxyAuth=%d challenges=%s]\n",
       this, mAuthChannel, proxyAuth, nsCString(aChallenge).get()));

  // this getter never fails
  nsHttpAuthCache* authCache = gHttpHandler->AuthCache(mIsPrivate);

  uint32_t authFlags;
  nsresult rv = auth->GetAuthFlags(&authFlags);
  if (NS_FAILED(rv)) return rv;

  nsAutoCString realm;
  ParseRealm(aChallenge, realm);

  // if no realm, then use the auth type as the realm.  ToUpperCase so the
  // ficticious realm stands out a bit more.
  // XXX this will cause some single signon misses!
  // XXX this was meant to be used with NTLM, which supplies no realm.
  /*
  if (realm.IsEmpty()) {
      realm = authType;
      ToUpperCase(realm);
  }
  */

  // set informations that depend on whether
  // we're authenticating against a proxy
  // or a webserver
  nsAutoCString host;
  int32_t port;
  nsHttpAuthIdentity* ident;
  nsAutoCString path, scheme;
  bool identFromURI = false;
  nsISupports** continuationState;

  rv = GetAuthorizationMembers(proxyAuth, scheme, host, port, path, ident,
                               continuationState);
  if (NS_FAILED(rv)) return rv;

  uint32_t loadFlags;
  rv = mAuthChannel->GetLoadFlags(&loadFlags);
  if (NS_FAILED(rv)) return rv;

  // Fill only for non-proxy auth, proxy credentials are not OA-isolated.
  nsAutoCString suffix;

  if (!proxyAuth) {
    nsCOMPtr<nsIChannel> chan = do_QueryInterface(mAuthChannel);
    GetOriginAttributesSuffix(chan, suffix);

    // if this is the first challenge, then try using the identity
    // specified in the URL.
    if (mIdent.IsEmpty()) {
      GetIdentityFromURI(authFlags, mIdent);
      identFromURI = !mIdent.IsEmpty();
    }

    if ((loadFlags & nsIRequest::LOAD_ANONYMOUS) && !identFromURI) {
      LOG(("Skipping authentication for anonymous non-proxy request\n"));
      return NS_ERROR_NOT_AVAILABLE;
    }

    // Let explicit URL credentials pass
    // regardless of the LOAD_ANONYMOUS flag
  } else if ((loadFlags & nsIRequest::LOAD_ANONYMOUS) && !UsingHttpProxy()) {
    LOG(("Skipping authentication for anonymous non-proxy request\n"));
    return NS_ERROR_NOT_AVAILABLE;
  }

  //
  // if we already tried some credentials for this transaction, then
  // we need to possibly clear them from the cache, unless the credentials
  // in the cache have changed, in which case we'd want to give them a
  // try instead.
  //
  RefPtr<nsHttpAuthEntry> entry;
  (void)authCache->GetAuthEntryForDomain(scheme, host, port, realm, suffix,
                                         entry);

  // hold reference to the auth session state (in case we clear our
  // reference to the entry).
  nsCOMPtr<nsISupports> sessionStateGrip;
  if (entry) sessionStateGrip = entry->mMetaData;

  // remember if we already had the continuation state.  it means we are in
  // the middle of the authentication exchange and the connection must be
  // kept sticky then (and only then).
  bool authAtProgress = !!*continuationState;

  // for digest auth, maybe our cached nonce value simply timed out...
  bool identityInvalid;
  nsISupports* sessionState = sessionStateGrip;
  rv = auth->ChallengeReceived(mAuthChannel, aChallenge, proxyAuth,
                               &sessionState, &*continuationState,
                               &identityInvalid);
  sessionStateGrip.swap(sessionState);
  if (NS_FAILED(rv)) return rv;

  LOG(("  identity invalid = %d\n", identityInvalid));

  if (mConnectionBased && identityInvalid) {
    // If the flag is set and identity is invalid, it means we received the
    // first challange for a new negotiation round after negotiating a
    // connection based auth failed (invalid password). The mConnectionBased
    // flag is set later for the newly received challenge, so here it reflects
    // the previous 401/7 response schema.
    rv = mAuthChannel->CloseStickyConnection();
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    if (!proxyAuth) {
      // We must clear proxy ident in the following scenario + explanation:
      // - we are authenticating to an NTLM proxy and an NTLM server
      // - we successfully authenticated to the proxy, mProxyIdent keeps
      //   the user name/domain and password, the identity has also been cached
      // - we just threw away the connection because we are now asking for
      //   creds for the server (WWW auth)
      // - hence, we will have to auth to the proxy again as well
      // - if we didn't clear the proxy identity, it would be considered
      //   as non-valid and we would ask the user again ; clearing it forces
      //   use of the cached identity and not asking the user again
      ClearProxyIdent();
    }
  }

  mConnectionBased = !!(authFlags & nsIHttpAuthenticator::CONNECTION_BASED);

  // It's legal if the peer closes the connection after the first 401/7.
  // Making the connection sticky will prevent its restart giving the user
  // a 'network reset' error every time.  Hence, we mark the connection
  // as restartable.
  mAuthChannel->ConnectionRestartable(!authAtProgress);

  if (identFromURI &&
      (authFlags & nsIHttpAuthenticator::IDENTITY_INCLUDES_DOMAIN)) {
    LOG(("  ignoring URL identity for a domain-identity scheme\n"));
    ident->Clear();
    identFromURI = false;
  }

  if (identityInvalid) {
    if (entry) {
      if (ident->Equals(entry->Identity())) {
        if (!identFromURI) {
          LOG(("  clearing bad auth cache entry\n"));
          // ok, we've already tried this user identity, so clear the
          // corresponding entry from the auth cache.
          authCache->ClearAuthEntry(scheme, host, port, realm, suffix);
          entry = nullptr;
          ident->Clear();
        }
      } else if (!identFromURI ||
                 (ident->User() == entry->Identity().User() &&
                  !(loadFlags & (nsIChannel::LOAD_ANONYMOUS |
                                 nsIChannel::LOAD_EXPLICIT_CREDENTIALS)))) {
        LOG(("  taking identity from auth cache\n"));
        // the password from the auth cache is more likely to be
        // correct than the one in the URL.  at least, we know that it
        // works with the given username.  it is possible for a server
        // to distinguish logons based on the supplied password alone,
        // but that would be quite unusual... and i don't think we need
        // to worry about such unorthodox cases.
        *ident = entry->Identity();
        identFromURI = false;
        if (entry->Creds()[0] != '\0') {
          LOG(("    using cached credentials!\n"));
          creds.Assign(entry->Creds());
          return entry->AddPath(path);
        }
      }
    } else if (!identFromURI) {
      // hmm... identity invalid, but no auth entry!  the realm probably
      // changed (see bug 201986).
      ident->Clear();
    }

    if (!entry && ident->IsEmpty()) {
      uint32_t level = nsIAuthPrompt2::LEVEL_NONE;
      if ((!proxyAuth && mUsingSSL) || (proxyAuth && mProxyUsingSSL)) {
        level = nsIAuthPrompt2::LEVEL_SECURE;
      } else if (authFlags & nsIHttpAuthenticator::IDENTITY_ENCRYPTED) {
        level = nsIAuthPrompt2::LEVEL_PW_ENCRYPTED;
      }

      // Depending on the pref setting, the authentication dialog may be
      // blocked for all sub-resources, blocked for cross-origin
      // sub-resources, or always allowed for sub-resources.
      // For more details look at the bug 647010.
      // BlockPrompt will set mCrossOrigin parameter as well.
      if (BlockPrompt(proxyAuth)) {
        LOG((
            "nsHttpChannelAuthProvider::GetCredentialsForChallenge: "
            "Prompt is blocked [this=%p pref=%d img-pref=%d "
            "non-web-content-triggered-pref=%d]\n",
            this, StaticPrefs::network_auth_subresource_http_auth_allow(),
            StaticPrefs::
                network_auth_subresource_img_cross_origin_http_auth_allow(),
            StaticPrefs::
                network_auth_non_web_content_triggered_resources_http_auth_allow()));
        return NS_ERROR_ABORT;
      }

      // at this point we are forced to interact with the user to get
      // their username and password for this domain.
      rv = PromptForIdentity(level, proxyAuth, realm, aAuthType, authFlags,
                             *ident);
      if (NS_FAILED(rv)) return rv;
      identFromURI = false;
    }
  }

  // get credentials for the given user:pass
  //
  // always store the credentials we're trying now so that they will be used
  // on subsequent links.  This will potentially remove good credentials from
  // the cache.  This is ok as we don't want to use cached credentials if the
  // user specified something on the URI or in another manner.  This is so
  // that we don't transparently authenticate as someone they're not
  // expecting to authenticate as.
  //
  nsCString result;
  rv = GenCredsAndSetEntry(auth, proxyAuth, scheme, host, port, path, realm,
                           aChallenge, *ident, sessionStateGrip, creds);
  return rv;
}

bool nsHttpChannelAuthProvider::BlockPrompt(bool proxyAuth) {
  // Verify that it's ok to prompt for credentials here, per spec
  // http://xhr.spec.whatwg.org/#the-send%28%29-method

  nsCOMPtr<nsIHttpChannelInternal> chanInternal =
      do_QueryInterface(mAuthChannel);
  MOZ_ASSERT(chanInternal);

  if (chanInternal->GetBlockAuthPrompt()) {
    LOG(
        ("nsHttpChannelAuthProvider::BlockPrompt: Prompt is blocked "
         "[this=%p channel=%p]\n",
         this, mAuthChannel));
    return true;
  }

  if (proxyAuth) {
    // Do not block auth-dialog if this is a proxy authentication.
    return false;
  }

  nsCOMPtr<nsIChannel> chan = do_QueryInterface(mAuthChannel);
  nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();

  // We will treat loads w/o loadInfo as a top level document.
  bool topDoc = true;
  bool xhr = false;
  bool nonWebContent = false;

  if (loadInfo->GetExternalContentPolicyType() !=
      ExtContentPolicy::TYPE_DOCUMENT) {
    topDoc = false;
  }

  if (!topDoc) {
    nsCOMPtr<nsIPrincipal> triggeringPrinc = loadInfo->TriggeringPrincipal();
    if (triggeringPrinc->IsSystemPrincipal()) {
      nonWebContent = true;
    }
  }

  if (loadInfo->GetExternalContentPolicyType() ==
      ExtContentPolicy::TYPE_XMLHTTPREQUEST) {
    xhr = true;
  }

  if (!topDoc && !xhr) {
    nsCOMPtr<nsIURI> topURI;
    (void)chanInternal->GetTopWindowURI(getter_AddRefs(topURI));
    if (topURI) {
      mCrossOrigin = !NS_SecurityCompareURIs(topURI, mURI, true);
    } else {
      nsIPrincipal* loadingPrinc = loadInfo->GetLoadingPrincipal();
      MOZ_ASSERT(loadingPrinc);
      mCrossOrigin = !loadingPrinc->IsSameOrigin(mURI);
    }
  }

  if (!topDoc &&
      !StaticPrefs::
          network_auth_non_web_content_triggered_resources_http_auth_allow() &&
      nonWebContent) {
    return true;
  }

  switch (StaticPrefs::network_auth_subresource_http_auth_allow()) {
    case SUBRESOURCE_AUTH_DIALOG_DISALLOW_ALL:
      // Do not open the http-authentication credentials dialog for
      // the sub-resources.
      return !topDoc && !xhr;
    case SUBRESOURCE_AUTH_DIALOG_DISALLOW_CROSS_ORIGIN:
      // Open the http-authentication credentials dialog for
      // the sub-resources only if they are not cross-origin.
      return !topDoc && !xhr && mCrossOrigin;
    case SUBRESOURCE_AUTH_DIALOG_ALLOW_ALL:
      // Allow the http-authentication dialog for subresources.
      // If pref network.auth.subresource-img-cross-origin-http-auth-allow
      // is set, http-authentication dialog for image subresources is
      // blocked.
      if (mCrossOrigin &&
          !StaticPrefs::
              network_auth_subresource_img_cross_origin_http_auth_allow() &&
          loadInfo &&
          ((loadInfo->GetExternalContentPolicyType() ==
            ExtContentPolicy::TYPE_IMAGE) ||
           (loadInfo->GetExternalContentPolicyType() ==
            ExtContentPolicy::TYPE_IMAGESET))) {
        return true;
      }
      return false;
    default:
      // This is an invalid value.
      MOZ_ASSERT(false, "A non valid value!");
  }
  return false;
}

inline void GetAuthType(const nsACString& aChallenge, nsCString& authType) {
  auto spaceIndex = aChallenge.FindChar(' ');
  authType = Substring(aChallenge, 0, spaceIndex);
  // normalize to lowercase
  ToLowerCase(authType);
}

nsresult nsHttpChannelAuthProvider::GetAuthenticator(
    const nsACString& aChallenge, nsCString& authType,
    nsIHttpAuthenticator** auth) {
  LOG(("nsHttpChannelAuthProvider::GetAuthenticator [this=%p channel=%p]\n",
       this, mAuthChannel));

  GetAuthType(aChallenge, authType);

  nsCOMPtr<nsIHttpAuthenticator> authenticator;
#ifdef MOZ_AUTH_EXTENSION
  if (authType.EqualsLiteral("negotiate")) {
    authenticator = nsHttpNegotiateAuth::GetOrCreate();
  } else
#endif
      if (authType.EqualsLiteral("basic")) {
    authenticator = nsHttpBasicAuth::GetOrCreate();
  } else if (authType.EqualsLiteral("digest")) {
    authenticator = nsHttpDigestAuth::GetOrCreate();
  } else if (authType.EqualsLiteral("ntlm")) {
    authenticator = nsHttpNTLMAuth::GetOrCreate();
  } else if (authType.EqualsLiteral("mock_auth") &&
             PR_GetEnv("XPCSHELL_TEST_PROFILE_DIR")) {
    authenticator = MockHttpAuth::Create();
  } else {
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  if (!authenticator) {
    // If called during shutdown it's possible that the singleton authenticator
    // was already cleared so we have a null one here.
    return NS_ERROR_NOT_AVAILABLE;
  }

  MOZ_ASSERT(authenticator);
  authenticator.forget(auth);

  return NS_OK;
}

// buf contains "domain\user"
static void ParseUserDomain(const nsAString& buf, nsDependentSubstring& user,
                            nsDependentSubstring& domain) {
  auto backslashPos = buf.FindChar(u'\\');
  if (backslashPos != kNotFound) {
    domain.Rebind(buf, 0, backslashPos);
    user.Rebind(buf, backslashPos + 1);
  }
}

void nsHttpChannelAuthProvider::GetIdentityFromURI(uint32_t authFlags,
                                                   nsHttpAuthIdentity& ident) {
  LOG(("nsHttpChannelAuthProvider::GetIdentityFromURI [this=%p channel=%p]\n",
       this, mAuthChannel));

  bool hasUserPass;
  if (NS_FAILED(mURI->GetHasUserPass(&hasUserPass)) || !hasUserPass) {
    return;
  }

  nsAutoString userBuf;
  nsAutoString passBuf;

  // XXX i18n
  nsAutoCString buf;
  nsresult rv = mURI->GetUsername(buf);
  if (NS_FAILED(rv)) {
    return;
  }
  NS_UnescapeURL(buf);
  CopyUTF8toUTF16(buf, userBuf);

  rv = mURI->GetPassword(buf);
  if (NS_FAILED(rv)) {
    return;
  }
  NS_UnescapeURL(buf);
  CopyUTF8toUTF16(buf, passBuf);

  nsDependentSubstring user(userBuf, 0);
  nsDependentSubstring domain(EmptyString(), 0);

  if (authFlags & nsIHttpAuthenticator::IDENTITY_INCLUDES_DOMAIN) {
    ParseUserDomain(userBuf, user, domain);
  }

  ident = nsHttpAuthIdentity(domain, user, passBuf);
}

void nsHttpChannelAuthProvider::ParseRealm(const nsACString& aChallenge,
                                           nsACString& realm) {
  //
  // From RFC2617 section 1.2, the realm value is defined as such:
  //
  //    realm       = "realm" "=" realm-value
  //    realm-value = quoted-string
  //
  // but, we'll accept anything after the the "=" up to the first space, or
  // end-of-line, if the string is not quoted.
  //

  Tokenizer t(aChallenge);

  // The challenge begins with the authType.
  // If we can't find that something has probably gone wrong.
  t.SkipWhites();
  nsDependentCSubstring authType;
  if (!t.ReadWord(authType)) {
    return;
  }

  // Will return true if the tokenizer advanced the cursor - false otherwise.
  auto readParam = [&](nsDependentCSubstring& key, nsAutoCString& value) {
    key.Rebind(EmptyCString(), 0);
    value.Truncate();

    t.SkipWhites();
    if (!t.ReadWord(key)) {
      return false;
    }
    t.SkipWhites();
    if (!t.CheckChar('=')) {
      return true;
    }
    t.SkipWhites();

    Tokenizer::Token token1;

    t.Record();
    if (!t.Next(token1)) {
      return true;
    }
    nsDependentCSubstring sub;
    bool hasQuote = false;
    if (token1.Equals(Tokenizer::Token::Char('"'))) {
      hasQuote = true;
    } else {
      t.Claim(sub, Tokenizer::ClaimInclusion::INCLUDE_LAST);
      value.Append(sub);
    }
    t.Record();
    Tokenizer::Token token2;
    while (t.Next(token2)) {
      if (hasQuote && token2.Equals(Tokenizer::Token::Char('"')) &&
          !token1.Equals(Tokenizer::Token::Char('\\'))) {
        break;
      }
      if (!hasQuote && (token2.Type() == Tokenizer::TokenType::TOKEN_WS ||
                        token2.Type() == Tokenizer::TokenType::TOKEN_EOL)) {
        break;
      }

      t.Claim(sub, Tokenizer::ClaimInclusion::INCLUDE_LAST);
      if (!sub.Equals(R"(\)")) {
        value.Append(sub);
      }
      t.Record();
      token1 = token2;
    }
    return true;
  };

  while (!t.CheckEOF()) {
    nsDependentCSubstring key;
    nsAutoCString value;
    // If we couldn't read anything, and the input isn't followed by a ,
    // then we exit.
    if (!readParam(key, value) && !t.Check(Tokenizer::Token::Char(','))) {
      break;
    }
    // When we find the first instance of realm we exit.
    // Theoretically there should be only one instance and we should fail
    // if there are more, but we're trying to preserve existing behaviour.
    if (key.Equals("realm"_ns, nsCaseInsensitiveCStringComparator)) {
      realm = value;
      break;
    }
  }
}

class nsHTTPAuthInformation : public nsAuthInformationHolder {
 public:
  nsHTTPAuthInformation(uint32_t aFlags, const nsString& aRealm,
                        const nsACString& aAuthType)
      : nsAuthInformationHolder(aFlags, aRealm, aAuthType) {}

  void SetToHttpAuthIdentity(uint32_t authFlags, nsHttpAuthIdentity& identity);
};

void nsHTTPAuthInformation::SetToHttpAuthIdentity(
    uint32_t authFlags, nsHttpAuthIdentity& identity) {
  identity = nsHttpAuthIdentity(Domain(), User(), Password());
}

nsresult nsHttpChannelAuthProvider::PromptForIdentity(
    uint32_t level, bool proxyAuth, const nsACString& realm,
    const nsACString& authType, uint32_t authFlags, nsHttpAuthIdentity& ident) {
  LOG(("nsHttpChannelAuthProvider::PromptForIdentity [this=%p channel=%p]\n",
       this, mAuthChannel));

  nsresult rv;

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  rv = mAuthChannel->GetNotificationCallbacks(getter_AddRefs(callbacks));
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsILoadGroup> loadGroup;
  rv = mAuthChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIAuthPrompt2> authPrompt;
  GetAuthPrompt(callbacks, proxyAuth, getter_AddRefs(authPrompt));
  if (!authPrompt && loadGroup) {
    nsCOMPtr<nsIInterfaceRequestor> cbs;
    loadGroup->GetNotificationCallbacks(getter_AddRefs(cbs));
    GetAuthPrompt(cbs, proxyAuth, getter_AddRefs(authPrompt));
  }
  if (!authPrompt) return NS_ERROR_NO_INTERFACE;

  // XXX i18n: need to support non-ASCII realm strings (see bug 41489)
  NS_ConvertASCIItoUTF16 realmU(realm);

  // prompt the user...
  uint32_t promptFlags = 0;
  if (proxyAuth) {
    promptFlags |= nsIAuthInformation::AUTH_PROXY;
    if (mTriedProxyAuth) promptFlags |= nsIAuthInformation::PREVIOUS_FAILED;
    mTriedProxyAuth = true;
  } else {
    promptFlags |= nsIAuthInformation::AUTH_HOST;
    if (mTriedHostAuth) promptFlags |= nsIAuthInformation::PREVIOUS_FAILED;
    mTriedHostAuth = true;
  }

  if (authFlags & nsIHttpAuthenticator::IDENTITY_INCLUDES_DOMAIN) {
    promptFlags |= nsIAuthInformation::NEED_DOMAIN;
  }

  if (mCrossOrigin) {
    promptFlags |= nsIAuthInformation::CROSS_ORIGIN_SUB_RESOURCE;
  }

  RefPtr<nsHTTPAuthInformation> holder =
      new nsHTTPAuthInformation(promptFlags, realmU, authType);
  if (!holder) return NS_ERROR_OUT_OF_MEMORY;

  nsCOMPtr<nsIChannel> channel(do_QueryInterface(mAuthChannel, &rv));
  if (NS_FAILED(rv)) return rv;

  rv = authPrompt->AsyncPromptAuth(channel, this, nullptr, level, holder,
                                   getter_AddRefs(mAsyncPromptAuthCancelable));

  if (NS_SUCCEEDED(rv)) {
    // indicate using this error code that authentication prompt
    // result is expected asynchronously
    rv = NS_ERROR_IN_PROGRESS;
  } else {
    // Fall back to synchronous prompt
    bool retval = false;
    rv = authPrompt->PromptAuth(channel, level, holder, &retval);
    if (NS_FAILED(rv)) return rv;

    if (!retval) {
      rv = NS_ERROR_ABORT;
    } else {
      holder->SetToHttpAuthIdentity(authFlags, ident);
    }
  }

  if (mConnectionBased) {
    // Connection can be reset by the server in the meantime user is entering
    // the credentials.  Result would be just a "Connection was reset" error.
    // Hence, we drop the current regardless if the user would make it on time
    // to provide credentials.
    // It's OK to send the NTLM type 1 message (response to the plain "NTLM"
    // challenge) on a new connection.
    {
      DebugOnly<nsresult> rv = mAuthChannel->CloseStickyConnection();
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }

  return rv;
}

NS_IMETHODIMP nsHttpChannelAuthProvider::OnAuthAvailable(
    nsISupports* aContext, nsIAuthInformation* aAuthInfo) {
  LOG(("nsHttpChannelAuthProvider::OnAuthAvailable [this=%p channel=%p]", this,
       mAuthChannel));

  mAsyncPromptAuthCancelable = nullptr;
  if (!mAuthChannel) return NS_OK;

  nsresult rv;

  nsAutoCString host;
  int32_t port;
  nsHttpAuthIdentity* ident;
  nsAutoCString path, scheme;
  nsISupports** continuationState;
  rv = GetAuthorizationMembers(mProxyAuth, scheme, host, port, path, ident,
                               continuationState);
  if (NS_FAILED(rv)) OnAuthCancelled(aContext, false);

  nsAutoCString realm;
  ParseRealm(mCurrentChallenge, realm);

  nsCOMPtr<nsIChannel> chan = do_QueryInterface(mAuthChannel);
  nsAutoCString suffix;
  if (!mProxyAuth) {
    // Fill only for non-proxy auth, proxy credentials are not OA-isolated.
    GetOriginAttributesSuffix(chan, suffix);
  }

  nsHttpAuthCache* authCache = gHttpHandler->AuthCache(mIsPrivate);
  RefPtr<nsHttpAuthEntry> entry;
  (void)authCache->GetAuthEntryForDomain(scheme, host, port, realm, suffix,
                                         entry);

  nsCOMPtr<nsISupports> sessionStateGrip;
  if (entry) sessionStateGrip = entry->mMetaData;

  nsString domain, user, password;
  aAuthInfo->GetDomain(domain);
  aAuthInfo->GetUsername(user);
  aAuthInfo->GetPassword(password);
  *ident = nsHttpAuthIdentity(domain, user, password);

  nsAutoCString unused;
  nsCOMPtr<nsIHttpAuthenticator> auth;
  rv = GetAuthenticator(mCurrentChallenge, unused, getter_AddRefs(auth));
  if (NS_FAILED(rv)) {
    MOZ_ASSERT(false, "GetAuthenticator failed");
    OnAuthCancelled(aContext, true);
    return NS_OK;
  }

  nsCString creds;
  rv = GenCredsAndSetEntry(auth, mProxyAuth, scheme, host, port, path, realm,
                           mCurrentChallenge, *ident, sessionStateGrip, creds);
  if (rv == NS_ERROR_IN_PROGRESS) {
    // OnCredsGenerated will be called and needs mCurrentChallenge.
    return NS_OK;
  }

  mCurrentChallenge.Truncate();
  if (NS_FAILED(rv)) {
    OnAuthCancelled(aContext, true);
    return NS_OK;
  }

  return ContinueOnAuthAvailable(creds);
}

NS_IMETHODIMP nsHttpChannelAuthProvider::OnAuthCancelled(nsISupports* aContext,
                                                         bool userCancel) {
  LOG(("nsHttpChannelAuthProvider::OnAuthCancelled [this=%p channel=%p]", this,
       mAuthChannel));

  mAsyncPromptAuthCancelable = nullptr;
  if (!mAuthChannel) return NS_OK;

  // When user cancels or auth fails we want to close the connection for
  // connection based schemes like NTLM.  Some servers don't like re-negotiation
  // on the same connection.
  nsresult rv;
  if (mConnectionBased) {
    rv = mAuthChannel->CloseStickyConnection();
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    mConnectionBased = false;
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(mAuthChannel);
  if (channel) {
    nsresult status;
    (void)channel->GetStatus(&status);
    if (NS_FAILED(status)) {
      // If the channel is already cancelled, there is no need to deal with the
      // rest challenges.
      LOG(("  Clear mRemainingChallenges, since mAuthChannel is cancelled"));
      mRemainingChallenges.Truncate();
    }
  }

  if (userCancel) {
    if (!mRemainingChallenges.IsEmpty()) {
      // there are still some challenges to process, do so

      // Get rid of current continuationState to avoid reusing it in
      // next challenges since it is no longer relevant.
      if (mProxyAuth) {
        NS_IF_RELEASE(mProxyAuthContinuationState);
        mProxyAuthType.Truncate();
      } else {
        NS_IF_RELEASE(mAuthContinuationState);
        mAuthType.Truncate();
      }
      nsAutoCString creds;
      rv = GetCredentials(mRemainingChallenges, mProxyAuth, creds);
      if (NS_SUCCEEDED(rv)) {
        // GetCredentials loaded the credentials from the cache or
        // some other way in a synchronous manner, process those
        // credentials now
        mRemainingChallenges.Truncate();
        return ContinueOnAuthAvailable(creds);
      }
      if (rv == NS_ERROR_IN_PROGRESS) {
        // GetCredentials successfully queued another authprompt for
        // a challenge from the list, we are now waiting for the user
        // to provide the credentials
        return NS_OK;
      }

      // otherwise, we failed...
    }

    mRemainingChallenges.Truncate();
  }

  rv = mAuthChannel->OnAuthCancelled(userCancel);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  return NS_OK;
}

NS_IMETHODIMP nsHttpChannelAuthProvider::OnCredsGenerated(
    const nsACString& aGeneratedCreds, uint32_t aFlags, nsresult aResult,
    nsISupports* aSessionState, nsISupports* aContinuationState) {
  nsresult rv;

  MOZ_ASSERT(NS_IsMainThread());

  // When channel is closed, do not proceed
  if (!mAuthChannel) {
    return NS_OK;
  }

  mGenerateCredentialsCancelable = nullptr;

  if (NS_FAILED(aResult)) {
    return OnAuthCancelled(nullptr, true);
  }

  // We want to update m(Proxy)AuthContinuationState in case it was changed by
  // nsHttpNegotiateAuth::GenerateCredentials
  nsCOMPtr<nsISupports> contState(aContinuationState);
  if (mProxyAuth) {
    contState.swap(mProxyAuthContinuationState);
  } else {
    contState.swap(mAuthContinuationState);
  }

  nsCOMPtr<nsIHttpAuthenticator> auth;
  nsAutoCString unused;
  rv = GetAuthenticator(mCurrentChallenge, unused, getter_AddRefs(auth));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString host;
  int32_t port;
  nsHttpAuthIdentity* ident;
  nsAutoCString directory, scheme;
  nsISupports** unusedContinuationState;

  // Get realm from challenge
  nsAutoCString realm;
  ParseRealm(mCurrentChallenge, realm);

  rv = GetAuthorizationMembers(mProxyAuth, scheme, host, port, directory, ident,
                               unusedContinuationState);
  if (NS_FAILED(rv)) return rv;

  rv =
      UpdateCache(auth, scheme, host, port, directory, realm, mCurrentChallenge,
                  *ident, aGeneratedCreds, aFlags, aSessionState, mProxyAuth);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  mCurrentChallenge.Truncate();

  rv = ContinueOnAuthAvailable(aGeneratedCreds);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  return NS_OK;
}

nsresult nsHttpChannelAuthProvider::ContinueOnAuthAvailable(
    const nsACString& creds) {
  nsresult rv;
  if (mProxyAuth) {
    rv = mAuthChannel->SetProxyCredentials(creds);
  } else {
    rv = mAuthChannel->SetWWWCredentials(creds);
  }
  if (NS_FAILED(rv)) return rv;

  // drop our remaining list of challenges.  We don't need them, because we
  // have now authenticated against a challenge and will be sending that
  // information to the server (or proxy).  If it doesn't accept our
  // authentication it'll respond with failure and resend the challenge list
  mRemainingChallenges.Truncate();

  (void)mAuthChannel->OnAuthAvailable();

  return NS_OK;
}

void nsHttpChannelAuthProvider::SetAuthorizationHeader(
    nsHttpAuthCache* authCache, const nsHttpAtom& header,
    const nsACString& scheme, const nsACString& host, int32_t port,
    const nsACString& path, nsHttpAuthIdentity& ident) {
  RefPtr<nsHttpAuthEntry> entry;
  nsresult rv;

  // set informations that depend on whether
  // we're authenticating against a proxy
  // or a webserver
  nsISupports** continuationState;

  nsAutoCString suffix;
  if (header == nsHttp::Proxy_Authorization) {
    continuationState = &mProxyAuthContinuationState;

    if (mProxyInfo) {
      nsAutoCString type;
      mProxyInfo->GetType(type);
      if (type.EqualsLiteral("https") || type.EqualsLiteral("masque")) {
        // Let this be overriden by anything from the cache.
        auto const& pa = mProxyInfo->ProxyAuthorizationHeader();
        if (!pa.IsEmpty()) {
          rv = mAuthChannel->SetProxyCredentials(pa);
          MOZ_ASSERT(NS_SUCCEEDED(rv));
        }
      }
    }
  } else {
    continuationState = &mAuthContinuationState;

    nsCOMPtr<nsIChannel> chan = do_QueryInterface(mAuthChannel);
    GetOriginAttributesSuffix(chan, suffix);
  }

  rv = authCache->GetAuthEntryForPath(scheme, host, port, path, suffix, entry);
  if (NS_SUCCEEDED(rv)) {
    // if we are trying to add a header for origin server auth and if the
    // URL contains an explicit username, then try the given username first.
    // we only want to do this, however, if we know the URL requires auth
    // based on the presence of an auth cache entry for this URL (which is
    // true since we are here).  but, if the username from the URL matches
    // the username from the cache, then we should prefer the password
    // stored in the cache since that is most likely to be valid.
    if (header == nsHttp::Authorization && entry->Domain()[0] == '\0') {
      GetIdentityFromURI(0, ident);
      // if the usernames match, then clear the ident so we will pick
      // up the one from the auth cache instead.
      // when this is undesired, specify LOAD_EXPLICIT_CREDENTIALS load
      // flag.
      if (ident.User() == entry->User()) {
        uint32_t loadFlags;
        if (NS_SUCCEEDED(mAuthChannel->GetLoadFlags(&loadFlags)) &&
            !(loadFlags & nsIChannel::LOAD_EXPLICIT_CREDENTIALS)) {
          ident.Clear();
        }
      }
    }
    bool identFromURI;
    if (ident.IsEmpty()) {
      ident = entry->Identity();
      identFromURI = false;
    } else {
      identFromURI = true;
    }

    nsCString temp;  // this must have the same lifetime as creds
    nsAutoCString creds(entry->Creds());
    // we can only send a preemptive Authorization header if we have either
    // stored credentials or a stored challenge from which to derive
    // credentials.  if the identity is from the URI, then we cannot use
    // the stored credentials.
    if ((creds.IsEmpty() || identFromURI) && !entry->Challenge().IsEmpty()) {
      nsCOMPtr<nsIHttpAuthenticator> auth;
      nsAutoCString unused;
      rv = GetAuthenticator(entry->Challenge(), unused, getter_AddRefs(auth));
      if (NS_SUCCEEDED(rv)) {
        bool proxyAuth = (header == nsHttp::Proxy_Authorization);
        rv = GenCredsAndSetEntry(auth, proxyAuth, scheme, host, port, path,
                                 entry->Realm(), entry->Challenge(), ident,
                                 entry->mMetaData, temp);
        if (NS_SUCCEEDED(rv)) creds = temp;

        // make sure the continuation state is null since we do not
        // support mixing preemptive and 'multirequest' authentication.
        NS_IF_RELEASE(*continuationState);
      }
    }
    if (!creds.IsEmpty()) {
      LOG(("   adding \"%s\" request header\n", header.get()));
      if (header == nsHttp::Proxy_Authorization) {
        rv = mAuthChannel->SetProxyCredentials(creds);
        MOZ_ASSERT(NS_SUCCEEDED(rv));
      } else {
        rv = mAuthChannel->SetWWWCredentials(creds);
        MOZ_ASSERT(NS_SUCCEEDED(rv));
      }
    } else {
      ident.Clear();  // don't remember the identity
    }
  }
}

nsresult nsHttpChannelAuthProvider::GetCurrentPath(nsACString& path) {
  nsresult rv;
  nsCOMPtr<nsIURL> url = do_QueryInterface(mURI);
  if (url) {
    rv = url->GetDirectory(path);
  } else {
    rv = mURI->GetPathQueryRef(path);
  }
  return rv;
}

NS_IMPL_ISUPPORTS(nsHttpChannelAuthProvider, nsICancelable,
                  nsIHttpChannelAuthProvider, nsIAuthPromptCallback,
                  nsIHttpAuthenticatorCallback)

}  // namespace mozilla::net
