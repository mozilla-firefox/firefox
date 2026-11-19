/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeCharsetUtils.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/StaticPrefs_network.h"

#include "nsAuthSASL.h"

nsAuthSASL::nsAuthSASL() { mSASLReady = false; }

void nsAuthSASL::Reset() { mSASLReady = false; }

/* Limitations apply to this class's thread safety. See the header file */
NS_IMPL_ISUPPORTS(nsAuthSASL, nsIAuthModule)

NS_IMETHODIMP
nsAuthSASL::Init(const nsACString& serviceName, uint32_t serviceFlags,
                 const nsAString& domain, const nsAString& username,
                 const nsAString& password) {
  NS_ASSERTION(!username.IsEmpty(), "SASL requires a username");
  NS_ASSERTION(domain.IsEmpty() && password.IsEmpty(),
               "unexpected credentials");

  mUsername = username;

  // If we're doing SASL, we should do mutual auth
  serviceFlags |= REQ_MUTUAL_AUTH;

  // Find out whether we should be trying SSPI or not
  const char* authType = "kerb-gss";
#ifdef XP_WIN
  if (mozilla::StaticPrefs::network_auth_use_sspi()) {
    authType = "kerb-sspi";
  }
#endif

  MOZ_ALWAYS_TRUE(mInnerModule = nsIAuthModule::CreateInstance(authType));

  return mInnerModule->Init(serviceName, serviceFlags, u""_ns, u""_ns, u""_ns);
}

NS_IMETHODIMP
nsAuthSASL::InitAsync(const nsACString& /*aServiceName*/,
                      uint32_t /*aServiceFlags*/, const nsAString& /*aDomain*/,
                      const nsAString& /*aUsername*/,
                      const nsAString& /*aPassword*/,
                      nsIAuthModuleInitCallback* /*aCallback*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsAuthSASL::GetNextToken(const void* inToken, uint32_t inTokenLen,
                         void** outToken, uint32_t* outTokenLen) {
  nsresult rv;
  void* unwrappedToken;
  char* message;
  uint32_t unwrappedTokenLen, messageLen;
  nsAutoCString userbuf;

  if (!mInnerModule) return NS_ERROR_NOT_INITIALIZED;

  if (mSASLReady) {
    // If the server COMPLETEs with an empty token, Cyrus sends us that token.
    // I don't think this is correct, but we need to handle that behaviour.
    // Cyrus ignores the contents of our reply token.
    if (inTokenLen == 0) {
      *outToken = nullptr;
      *outTokenLen = 0;
      return NS_OK;
    }
    // We've completed the GSSAPI portion of the handshake, and are
    // now ready to do the SASL security layer and authzid negotiation

    // Input packet from the server needs to be unwrapped.
    rv = mInnerModule->Unwrap(inToken, inTokenLen, &unwrappedToken,
                              &unwrappedTokenLen);
    if (NS_FAILED(rv)) {
      Reset();
      return rv;
    }

    // If we were doing security layers then we'd care what the
    // server had sent us. We're not, so all we had to do was make
    // sure that the signature was correct with the above unwrap()
    free(unwrappedToken);

    NS_CopyUnicodeToNative(mUsername, userbuf);
    messageLen = userbuf.Length() + 4 + 1;
    message = (char*)moz_xmalloc(messageLen);
    message[0] = 0x01;  // No security layer
    message[1] = 0x00;
    message[2] = 0x00;
    message[3] = 0x00;  // Maxbuf must be zero if we've got no sec layer
    strcpy(message + 4, userbuf.get());
    // Userbuf should not be nullptr terminated, so trim the trailing nullptr
    // when wrapping the message
    rv = mInnerModule->Wrap((void*)message, messageLen - 1, false, outToken,
                            outTokenLen);
    free(message);
    Reset();  // All done
    return NS_SUCCEEDED(rv) ? NS_SUCCESS_AUTH_FINISHED : rv;
  }
  rv = mInnerModule->GetNextToken(inToken, inTokenLen, outToken, outTokenLen);
  if (rv == NS_SUCCESS_AUTH_FINISHED) {
    mSASLReady = true;
    rv = NS_OK;
  }
  return rv;
}

NS_IMETHODIMP
nsAuthSASL::Unwrap(const void* /*inToken*/, uint32_t /*inTokenLen*/,
                   void** /*outToken*/, uint32_t* /*outTokenLen*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsAuthSASL::Wrap(const void* /*inToken*/, uint32_t /*inTokenLen*/,
                 bool /*confidential*/, void** /*outToken*/,
                 uint32_t* /*outTokenLen*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
