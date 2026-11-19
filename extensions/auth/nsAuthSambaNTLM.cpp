/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/process_util.h"
#include "nsAuth.h"
#include "nsAuthSambaNTLM.h"
#include "nspr.h"
#include "prenv.h"
#include "prerror.h"
#include "mozilla/Atomics.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"
#include "mozilla/Base64.h"

#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

nsAuthSambaNTLM::nsAuthSambaNTLM() = default;

nsAuthSambaNTLM::~nsAuthSambaNTLM() {
  // ntlm_auth reads from stdin regularly so closing our file handles
  // should cause it to exit.
  Shutdown();
  PR_Free(mInitialMessage);
}

void nsAuthSambaNTLM::Shutdown() {
  mFromChildFD = nullptr;
  mToChildFD = nullptr;

  if (mChildPID != -1) {
    // Kill and wait for the process to exit.
    kill(mChildPID, SIGKILL);

    int status = 0;
    pid_t result;
    do {
      result = waitpid(mChildPID, &status, 0);
    } while (result == -1 && errno == EINTR);

    mChildPID = -1;
  }
}

NS_IMPL_ISUPPORTS(nsAuthSambaNTLM, nsIAuthModule)

[[nodiscard]] static bool CreatePipe(mozilla::UniqueFileHandle* aReadPipe,
                                     mozilla::UniqueFileHandle* aWritePipe) {
  int fds[2];
  if (pipe(fds) == -1) {
    return false;
  }

  aReadPipe->reset(fds[0]);
  aWritePipe->reset(fds[1]);
  return true;
}

static bool WriteString(const mozilla::UniqueFileHandle& aFD,
                        const nsACString& aString) {
  size_t length = aString.Length();
  const char* s = aString.BeginReading();
  LOG(("Writing to ntlm_auth: %s", PromiseFlatCString(aString).get()));

  while (length > 0) {
    ssize_t result;
    do {
      result = write(aFD.get(), s, length);
    } while (result == -1 && errno == EINTR);
    if (result <= 0) return false;
    s += result;
    length -= result;
  }
  return true;
}

static bool ReadLine(const mozilla::UniqueFileHandle& aFD,
                     nsACString& aString) {
  // ntlm_auth is defined to only send one line in response to each of our
  // input lines. So this simple unbuffered strategy works as long as we
  // read the response immediately after sending one request.
  aString.Truncate();
  for (;;) {
    char buf[1024];
    ssize_t result;
    do {
      result = read(aFD.get(), buf, sizeof(buf));
    } while (result == -1 && errno == EINTR);
    if (result <= 0) return false;
    aString.Append(buf, result);
    if (buf[result - 1] == '\n') {
      LOG(("Read from ntlm_auth: %s", nsPromiseFlatCString(aString).get()));
      return true;
    }
  }
}

/**
 * Returns a heap-allocated array of PRUint8s, and stores the length in aLen.
 * Returns nullptr if there's an error of any kind.
 */
static uint8_t* ExtractMessage(const nsACString& aLine, uint32_t* aLen) {
  // ntlm_auth sends blobs to us as base64-encoded strings after the "xx "
  // preamble on the response line.
  int32_t length = aLine.Length();
  // The caller should verify there is a valid "xx " prefix and the line
  // is terminated with a \n
  NS_ASSERTION(length >= 4, "Line too short...");
  const char* line = aLine.BeginReading();
  const char* s = line + 3;
  length -= 4;  // lose first 3 chars plus trailing \n
  NS_ASSERTION(s[length] == '\n', "aLine not newline-terminated");

  if (length & 3) {
    // The base64 encoded block must be multiple of 4. If not, something
    // screwed up.
    NS_WARNING("Base64 encoded block should be a multiple of 4 chars");
    return nullptr;
  }

  char* base64;
  if (NS_FAILED(mozilla::Base64Decode(s, length, &base64, aLen))) {
    return nullptr;
  }
  return (uint8_t*)base64;
}

nsresult nsAuthSambaNTLM::SpawnNTLMAuthHelper() {
  const char* username = PR_GetEnv("USER");
  if (!username) return NS_ERROR_FAILURE;

  // Use base::LaunchApp to run the child process. This code is posix-only, as
  // this will not be used on Windows.
  {
    mozilla::UniqueFileHandle toChildPipeRead;
    mozilla::UniqueFileHandle toChildPipeWrite;
    if (!CreatePipe(&toChildPipeRead, &toChildPipeWrite)) {
      return NS_ERROR_FAILURE;
    }

    mozilla::UniqueFileHandle fromChildPipeRead;
    mozilla::UniqueFileHandle fromChildPipeWrite;
    if (!CreatePipe(&fromChildPipeRead, &fromChildPipeWrite)) {
      return NS_ERROR_FAILURE;
    }

    base::LaunchOptions options;
    options.fds_to_remap.push_back(
        std::pair{toChildPipeRead.get(), STDIN_FILENO});
    options.fds_to_remap.push_back(
        std::pair{fromChildPipeWrite.get(), STDOUT_FILENO});

    std::vector<std::string> argvVec{"ntlm_auth",        "--helper-protocol",
                                     "ntlmssp-client-1", "--use-cached-creds",
                                     "--username",       username};

    auto result = base::LaunchApp(argvVec, std::move(options), &mChildPID);
    if (result.isErr()) {
      return NS_ERROR_FAILURE;
    }

    mToChildFD = std::move(toChildPipeWrite);
    mFromChildFD = std::move(fromChildPipeRead);
  }

  if (!WriteString(mToChildFD, "YR\n"_ns)) return NS_ERROR_FAILURE;
  nsCString line;
  if (!ReadLine(mFromChildFD, line)) return NS_ERROR_FAILURE;
  if (!StringBeginsWith(line, "YR "_ns)) {
    // Something went wrong. Perhaps no credentials are accessible.
    return NS_ERROR_FAILURE;
  }

  // It gave us an initial client-to-server request packet. Save that
  // because we'll need it later.
  mInitialMessage = ExtractMessage(line, &mInitialMessageLen);
  if (!mInitialMessage) return NS_ERROR_FAILURE;
  return NS_OK;
}

NS_IMETHODIMP
nsAuthSambaNTLM::Init(const nsACString& /*serviceName*/, uint32_t serviceFlags,
                      const nsAString& domain, const nsAString& username,
                      const nsAString& password) {
  NS_ASSERTION(username.IsEmpty() && domain.IsEmpty() && password.IsEmpty(),
               "unexpected credentials");

  // Init runs on background threads, so several auths may reach this
  // concurrently.
  static mozilla::Atomic<bool> sTelemetrySent{false};
  if (sTelemetrySent.compareExchange(false, true)) {
    mozilla::glean::security::ntlm_module_used.AccumulateSingleSample(
        serviceFlags & nsIAuthModule::REQ_PROXY_AUTH
            ? NTLM_MODULE_SAMBA_AUTH_PROXY
            : NTLM_MODULE_SAMBA_AUTH_DIRECT);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAuthSambaNTLM::InitAsync(const nsACString& /*aServiceName*/,
                           uint32_t /*aServiceFlags*/,
                           const nsAString& /*aDomain*/,
                           const nsAString& /*aUsername*/,
                           const nsAString& /*aPassword*/,
                           nsIAuthModuleInitCallback* /*aCallback*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsAuthSambaNTLM::GetNextToken(const void* inToken, uint32_t inTokenLen,
                              void** outToken, uint32_t* outTokenLen) {
  if (!inToken) {
    /* someone wants our initial message */
    *outToken = moz_xmemdup(mInitialMessage, mInitialMessageLen);
    *outTokenLen = mInitialMessageLen;
    return NS_OK;
  }

  /* inToken must be a type 2 message. Get ntlm_auth to generate our response */
  nsCString request;
  request.AssignLiteral("TT ");
  if (NS_FAILED(mozilla::Base64EncodeAppend(static_cast<const char*>(inToken),
                                            inTokenLen, request))) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  request.Append('\n');

  if (!WriteString(mToChildFD, request)) return NS_ERROR_FAILURE;
  nsCString line;
  if (!ReadLine(mFromChildFD, line)) return NS_ERROR_FAILURE;
  if (!StringBeginsWith(line, "KK "_ns) && !StringBeginsWith(line, "AF "_ns)) {
    // Something went wrong. Perhaps no credentials are accessible.
    return NS_ERROR_FAILURE;
  }
  uint8_t* buf = ExtractMessage(line, outTokenLen);
  if (!buf) return NS_ERROR_FAILURE;
  *outToken = moz_xmemdup(buf, *outTokenLen);
  PR_Free(buf);

  // We're done. Close our file descriptors now and reap the helper
  // process.
  Shutdown();
  return NS_SUCCESS_AUTH_FINISHED;
}

NS_IMETHODIMP
nsAuthSambaNTLM::Unwrap(const void* /*inToken*/, uint32_t /*inTokenLen*/,
                        void** /*outToken*/, uint32_t* /*outTokenLen*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsAuthSambaNTLM::Wrap(const void* /*inToken*/, uint32_t /*inTokenLen*/,
                      bool /*confidential*/, void** /*outToken*/,
                      uint32_t* /*outTokenLen*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
