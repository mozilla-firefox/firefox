/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsStandardURL.h"

#include <string.h>

#include "IPv4Parser.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Logging.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/TextUtils.h"
#include "mozilla/Utf8.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/MozURL_ffi.h"
#include "nsASCIIMask.h"
#include "nsCRT.h"
#include "nsContentUtils.h"
#include "nsEscape.h"
#include "nsIClassInfoImpl.h"
#include "nsIFile.h"
#include "nsIIDNService.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIURLParser.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsURLParsers.h"
#include "prprf.h"

//
// setenv MOZ_LOG nsStandardURL:5
//
static mozilla::LazyLogModule gStandardURLLog("nsStandardURL");

// The Chromium code defines its own LOG macro which we don't want
#undef LOG
#define LOG(args) MOZ_LOG(gStandardURLLog, LogLevel::Debug, args)
#undef LOG_ENABLED
#define LOG_ENABLED() MOZ_LOG_TEST(gStandardURLLog, LogLevel::Debug)

using namespace mozilla::ipc;

/**
 * The UTS #46 ToUnicode operation as parametrized by the WHATWG URL Standard,
 * except potentially misleading labels are treated according to ToASCII
 * instead. Combined with the ToASCII operation without rerunning the expensive
 * part.
 *
 * NOTE: This function performs percent-decoding on the argument unlike
 * the other `NS_DomainTo` functions!
 *
 * If upon successfull return `aASCII` is empty, it is the caller's
 * responsibility to treat the value of `aDisplay` also as the value of
 * `aASCII`. (The weird semantics avoid useless allocation / copying.)
 *
 * Rust callers that don't happen to be using XPCOM strings are better
 * off using the `idna` crate directly. (See `idna_glue` for what policy
 * closure to use.)
 */
inline nsresult NS_DomainToDisplayAndASCII(const nsACString& aDomain,
                                           nsACString& aDisplay,
                                           nsACString& aASCII) {
  return mozilla_net_domain_to_display_and_ascii_impl(&aDomain, &aDisplay,
                                                      &aASCII);
}

namespace mozilla {
namespace net {

static NS_DEFINE_CID(kThisImplCID, NS_THIS_STANDARDURL_IMPL_CID);

// This will always be initialized and destroyed on the main thread, but
// can be safely used on other threads.
StaticRefPtr<nsIIDNService> nsStandardURL::gIDN;

Atomic<bool, Relaxed> nsStandardURL::gInitialized{false};

const char nsStandardURL::gHostLimitDigits[] = {'/', '\\', '?', '#', 0};

//----------------------------------------------------------------------------
// nsStandardURL::nsSegmentEncoder
//----------------------------------------------------------------------------

nsStandardURL::nsSegmentEncoder::nsSegmentEncoder(const Encoding* encoding)
    : mEncoding(encoding) {
  if (mEncoding == UTF_8_ENCODING) {
    mEncoding = nullptr;
  }
}

int32_t nsStandardURL::nsSegmentEncoder::EncodeSegmentCount(
    const char* aStr, const URLSegment& aSeg, int16_t aMask, nsCString& aOut,
    bool& aAppended, uint32_t aExtraLen) {
  // aExtraLen is characters outside the segment that will be
  // added when the segment is not empty (like the @ following
  // a username).
  if (!aStr || aSeg.mLen <= 0) {
    // Empty segment, so aExtraLen not added per above.
    aAppended = false;
    return 0;
  }

  uint32_t origLen = aOut.Length();

  Span<const char> span = Span(aStr + aSeg.mPos, aSeg.mLen);

  // first honor the origin charset if appropriate. as an optimization,
  // only do this if the segment is non-ASCII.  Further, if mEncoding is
  // null, then the origin charset is UTF-8 and there is nothing to do.
  if (mEncoding) {
    size_t upTo;
    if (MOZ_UNLIKELY(mEncoding == ISO_2022_JP_ENCODING)) {
      upTo = Encoding::ISO2022JPASCIIValidUpTo(AsBytes(span));
    } else {
      upTo = Encoding::ASCIIValidUpTo(AsBytes(span));
    }
    if (upTo != span.Length()) {
      // we have to encode this segment
      char bufferArr[512];
      Span<char> buffer = Span(bufferArr);

      auto encoder = mEncoding->NewEncoder();

      nsAutoCString valid;  // has to be declared in this scope
      if (MOZ_UNLIKELY(!IsUtf8(span.From(upTo)))) {
        // It's UB to pass invalid UTF-8 to
        // EncodeFromUTF8WithoutReplacement(), so let's make our input valid
        // UTF-8 by replacing invalid sequences with the REPLACEMENT
        // CHARACTER.
        UTF_8_ENCODING->Decode(
            nsDependentCSubstring(span.Elements(), span.Length()), valid);
        // This assigment is OK. `span` can't be used after `valid` has
        // been destroyed because the only way out of the scope that `valid`
        // was declared in is via return inside the loop below. Specifically,
        // the return is the only way out of the loop.
        span = valid;
      }

      size_t totalRead = 0;
      for (;;) {
        auto [encoderResult, read, written] =
            encoder->EncodeFromUTF8WithoutReplacement(
                AsBytes(span.From(totalRead)), AsWritableBytes(buffer), true);
        totalRead += read;
        auto bufferWritten = buffer.To(written);
        if (!NS_EscapeURLSpan(bufferWritten, aMask, aOut)) {
          aOut.Append(bufferWritten);
        }
        if (encoderResult == kInputEmpty) {
          aAppended = true;
          // Difference between original and current output
          // string lengths plus extra length
          return aOut.Length() - origLen + aExtraLen;
        }
        if (encoderResult == kOutputFull) {
          continue;
        }
        aOut.AppendLiteral("%26%23");
        aOut.AppendInt(encoderResult);
        aOut.AppendLiteral("%3B");
      }
      MOZ_RELEASE_ASSERT(
          false,
          "There's supposed to be no way out of the above loop except return.");
    }
  }

  if (NS_EscapeURLSpan(span, aMask, aOut)) {
    aAppended = true;
    // Difference between original and current output
    // string lengths plus extra length
    return aOut.Length() - origLen + aExtraLen;
  }
  aAppended = false;
  // Original segment length plus extra length
  return span.Length() + aExtraLen;
}

const nsACString& nsStandardURL::nsSegmentEncoder::EncodeSegment(
    const nsACString& str, int16_t mask, nsCString& result) {
  const char* text;
  bool encoded;
  EncodeSegmentCount(str.BeginReading(text), URLSegment(0, str.Length()), mask,
                     result, encoded);
  if (encoded) {
    return result;
  }
  return str;
}

//----------------------------------------------------------------------------
// nsStandardURL <public>
//----------------------------------------------------------------------------

#ifdef DEBUG_DUMP_URLS_AT_SHUTDOWN
static StaticMutex gAllURLsMutex;

// Deliberately leaked (never destroyed): nsStandardURL objects may still be
// alive when the process shuts down abnormally (e.g. Ctrl+C from a console on
// Windows), and destroying a non-empty list would abort a DEBUG build with a
// MOZ_CRASH during DLL teardown.
static LinkedList<nsStandardURL>* gAllURLs MOZ_GUARDED_BY(gAllURLsMutex) =
    nullptr;
#endif

nsStandardURL::nsStandardURL(bool aSupportsFileURL, bool aTrackURL)
    : mURLType(URLTYPE_STANDARD),
      mSupportsFileURL(aSupportsFileURL),
      mCheckedIfHostA(false) {
  LOG(("Creating nsStandardURL @%p\n", this));

  // gInitialized changes value only once (false->true) on the main thread.
  // It's OK to race here because in the worst case we'll just
  // dispatch a noop runnable to the main thread.
  MOZ_ASSERT(gInitialized);

  // default parser in case nsIStandardURL::Init is never called
  mParser = net_GetStdURLParser();

#ifdef DEBUG_DUMP_URLS_AT_SHUTDOWN
  if (aTrackURL) {
    StaticMutexAutoLock lock(gAllURLsMutex);
    if (!gAllURLs) {
      gAllURLs = new LinkedList<nsStandardURL>();
    }
    gAllURLs->insertBack(this);
  }
#endif
}

// Reason codes reported by IsValid() to identify the first failing check.
// Used to disambiguate SanityCheck() crashes in the field.
enum InvalidURLReason : uint32_t {
  eURLValid = 0,
  eURLSegmentBadLen = 1,
  eURLSegmentOutOfString = 2,
  eURLSegmentOverflow = 3,
  eURLSchemeNotAtStart = 4,
  eURLEmbeddedNul = 5,
  eURLSchemeNoColon = 6,
};

bool nsStandardURL::IsValid(uint32_t* aFailReason) {
  auto fail = [&](uint32_t aReason) {
    if (aFailReason) {
      *aFailReason = aReason;
    }
    return false;
  };

  auto checkSegment = [&](const nsStandardURL::URLSegment& aSeg) {
#ifdef NIGHTLY_BUILD
    // If the parity is not the same, we assume that this is caused by a memory
    // error. In this case, we think this URLSegment is valid.
    if ((aSeg.mPos.Parity() != aSeg.mPos.CalculateParity()) ||
        (aSeg.mLen.Parity() != aSeg.mLen.CalculateParity())) {
      MOZ_ASSERT(false);
      return true;
    }
#endif
    // Bad value
    if (NS_WARN_IF(aSeg.mLen < -1)) {
      return fail(eURLSegmentBadLen);
    }
    if (aSeg.mLen == -1) {
      return true;
    }

    // Points out of string
    if (NS_WARN_IF(aSeg.mPos + aSeg.mLen > mSpec.Length())) {
      return fail(eURLSegmentOutOfString);
    }

    // Overflow
    if (NS_WARN_IF(aSeg.mPos + aSeg.mLen < aSeg.mPos)) {
      return fail(eURLSegmentOverflow);
    }

    return true;
  };

  bool allSegmentsValid = checkSegment(mScheme) && checkSegment(mAuthority) &&
                          checkSegment(mUsername) && checkSegment(mPassword) &&
                          checkSegment(mHost) && checkSegment(mPath) &&
                          checkSegment(mFilepath) && checkSegment(mDirectory) &&
                          checkSegment(mBasename) && checkSegment(mExtension) &&
                          checkSegment(mQuery) && checkSegment(mRef);
  if (!allSegmentsValid) {
    return false;
  }

  if (mScheme.mPos != 0) {
    return fail(eURLSchemeNotAtStart);
  }

  // mSpec must not contain embedded NULs
  if (NS_WARN_IF(mSpec.FindChar('\0') != -1)) {
    return fail(eURLEmbeddedNul);
  }

  // The character immediately after the scheme must be ':', e.g. "http:".
  if (mScheme.mLen > 0 && NS_WARN_IF(mSpec.CharAt(mScheme.mLen) != ':')) {
    return fail(eURLSchemeNoColon);
  }

  return true;
}

void nsStandardURL::SanityCheck() {
  // Record which check failed so field crashes can be disambiguated
  uint32_t failReason = eURLValid;
  if (!IsValid(&failReason)) {
    nsPrintfCString msg(
        "reason:%X, mLen:%zX, mScheme (%X,%X), mAuthority (%X,%X), mUsername "
        "(%X,%X), mPassword (%X,%X), mHost (%X,%X), mPath (%X,%X), mFilepath "
        "(%X,%X), mDirectory (%X,%X), mBasename (%X,%X), mExtension (%X,%X), "
        "mQuery (%X,%X), mRef (%X,%X)",
        failReason, mSpec.Length(), (uint32_t)mScheme.mPos,
        (int32_t)mScheme.mLen, (uint32_t)mAuthority.mPos,
        (int32_t)mAuthority.mLen, (uint32_t)mUsername.mPos,
        (int32_t)mUsername.mLen, (uint32_t)mPassword.mPos,
        (int32_t)mPassword.mLen, (uint32_t)mHost.mPos, (int32_t)mHost.mLen,
        (uint32_t)mPath.mPos, (int32_t)mPath.mLen, (uint32_t)mFilepath.mPos,
        (int32_t)mFilepath.mLen, (uint32_t)mDirectory.mPos,
        (int32_t)mDirectory.mLen, (uint32_t)mBasename.mPos,
        (int32_t)mBasename.mLen, (uint32_t)mExtension.mPos,
        (int32_t)mExtension.mLen, (uint32_t)mQuery.mPos, (int32_t)mQuery.mLen,
        (uint32_t)mRef.mPos, (int32_t)mRef.mLen);
    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::URLSegments, msg);

    MOZ_CRASH("nsStandardURL::SanityCheck failed");
  }
}

nsStandardURL::~nsStandardURL() {
  LOG(("Destroying nsStandardURL @%p\n", this));

#ifdef DEBUG_DUMP_URLS_AT_SHUTDOWN
  {
    StaticMutexAutoLock lock(gAllURLsMutex);
    if (isInList()) {
      remove();
    }
  }
#endif
}

#ifdef DEBUG_DUMP_URLS_AT_SHUTDOWN
struct DumpLeakedURLs {
  DumpLeakedURLs() = default;
  ~DumpLeakedURLs();
};

DumpLeakedURLs::~DumpLeakedURLs() {
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(gAllURLsMutex);
  if (gAllURLs && !gAllURLs->isEmpty()) {
    printf("Leaked URLs:\n");
    for (auto* url : *gAllURLs) {
      url->PrintSpec();
    }
    gAllURLs->clear();
  }
}
#endif

void nsStandardURL::InitGlobalObjects() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  if (gInitialized) {
    return;
  }

  gInitialized = true;

  nsCOMPtr<nsIIDNService> serv(do_GetService(NS_IDNSERVICE_CONTRACTID));
  if (serv) {
    gIDN = serv;
  }
  MOZ_DIAGNOSTIC_ASSERT(gIDN);

  // Make sure nsURLHelper::InitGlobals() gets called on the main thread
  RefPtr<nsBaseURLParser> parser = net_GetStdURLParser();
  MOZ_DIAGNOSTIC_ASSERT(parser);
  (void)parser;
}

void nsStandardURL::ShutdownGlobalObjects() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  gIDN = nullptr;

#ifdef DEBUG_DUMP_URLS_AT_SHUTDOWN
  if (gInitialized) {
    // This instanciates a dummy class, and will trigger the class
    // destructor when libxul is unloaded. This is equivalent to atexit(),
    // but gracefully handles dlclose().
    StaticMutexAutoLock lock(gAllURLsMutex);
    static DumpLeakedURLs d;
  }
#endif
}

//----------------------------------------------------------------------------
// nsStandardURL <private>
//----------------------------------------------------------------------------

void nsStandardURL::Clear() {
  mSpec.Truncate();
  ResetSpecHash();

  mPort = -1;

  mScheme.Reset();
  mAuthority.Reset();
  mUsername.Reset();
  mPassword.Reset();
  mHost.Reset();

  mPath.Reset();
  mFilepath.Reset();
  mDirectory.Reset();
  mBasename.Reset();

  mExtension.Reset();
  mQuery.Reset();
  mRef.Reset();

  InvalidateCache();
}

void nsStandardURL::InvalidateCache(bool invalidateCachedFile) {
  if (invalidateCachedFile) {
    mFile = nullptr;
  }
}

nsIIDNService* nsStandardURL::GetIDNService() { return gIDN.get(); }

nsresult nsStandardURL::NormalizeIDN(const nsACString& aHost,
                                     nsACString& aResult) {
  mDisplayHost.Truncate();
  mCheckedIfHostA = true;
  nsCString displayHost;  // Intentionally not nsAutoCString to avoid copy when
                          // assigning to field
  nsresult rv = NS_DomainToDisplayAndASCII(aHost, displayHost, aResult);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (aResult.IsEmpty()) {
    aResult.Assign(displayHost);
  } else {
    mDisplayHost = std::move(displayHost);
  }
  return NS_OK;
}

void nsStandardURL::CoalescePath(char* path) {
  // Every rewrite in net_CoalesceDirs keys off a '/' followed by '.' (for
  // /./ and /../) or by '%' (for the percent-encoded %2e forms), so without
  // such a pair before the path terminator both of its scans are no-ops.
  bool needsCoalesce = false;
  for (const char* p = path; *p && *p != '?' && *p != '#'; ++p) {
    if (*p == '/' && (p[1] == '.' || p[1] == '%')) {
      needsCoalesce = true;
      break;
    }
  }
  if (!needsCoalesce) {
    return;
  }

  auto resultCoalesceDirs = net_CoalesceDirs(path);
  int32_t newLen = strlen(path);
  if (newLen < mPath.mLen && resultCoalesceDirs) {
    // Obtain indices for the last slash and the end of the basename
    uint32_t lastSlash = resultCoalesceDirs->first();
    uint32_t endOfBasename = resultCoalesceDirs->second();

    int32_t diff = newLen - mPath.mLen;
    mPath.mLen = newLen;

    // The directory length includes all characters up to and
    // including the last slash
    mDirectory.mLen = static_cast<int32_t>(lastSlash) + 1;

    // basename length includes everything after the last slash
    // until hash, query, or the null char. However, if there was an extension
    // we must make sure to update the length.
    mBasename.mLen = static_cast<int32_t>(endOfBasename - mDirectory.mLen);
    if (mExtension.mLen >= 0) {
      mBasename.mLen -= 1;  // Length of the . character
      mBasename.mLen -= mExtension.mLen;
    }
    mBasename.mPos = mDirectory.mPos + mDirectory.mLen;

    // Adjust the positions of extension, query, and ref as needed
    // This is possible because net_CoalesceDirs does not modify their lengths
    ShiftFromExtension(diff);

    mFilepath.mLen += diff;
  }
}

uint32_t nsStandardURL::AppendSegmentToBuf(char* buf, uint32_t i,
                                           const char* str,
                                           const URLSegment& segInput,
                                           URLSegment& segOutput,
                                           const nsCString* escapedStr,
                                           bool useEscaped, int32_t* diff) {
  MOZ_ASSERT(segInput.mLen == segOutput.mLen);

  if (diff) {
    *diff = 0;
  }

  if (segInput.mLen > 0) {
    if (useEscaped) {
      MOZ_ASSERT(diff);
      segOutput.mLen = escapedStr->Length();
      *diff = segOutput.mLen - segInput.mLen;
      memcpy(buf + i, escapedStr->get(), segOutput.mLen);
    } else {
      memcpy(buf + i, str + segInput.mPos, segInput.mLen);
    }
    segOutput.mPos = i;
    i += segOutput.mLen;
  } else {
    segOutput.mPos = i;
  }
  return i;
}

uint32_t nsStandardURL::AppendToBuf(char* buf, uint32_t i, const char* str,
                                    uint32_t len) {
  memcpy(buf + i, str, len);
  return i + len;
}

// basic algorithm:
//  1- escape url segments (for improved GetSpec efficiency)
//  2- allocate spec buffer
//  3- write url segments
//  4- update url segment positions and lengths
nsresult nsStandardURL::BuildNormalizedSpec(const char* spec,
                                            const Encoding* encoding) {
  // Assumptions: all member URLSegments must be relative the |spec| argument
  // passed to this function.

  // buffers for holding escaped url segments (these will remain empty unless
  // escaping is required).
  nsAutoCString encUsername, encPassword, encHost, encDirectory, encBasename,
      encExtension, encQuery, encRef;
  // EncodeSegmentCount only writes these when its segment is present, and the
  // path fast path below reads all of them unconditionally.
  bool useEncUsername = false, useEncPassword = false, useEncHost = false,
       useEncDirectory = false, useEncBasename = false, useEncExtension = false,
       useEncQuery = false, useEncRef = false;
  nsAutoCString portbuf;

  //
  // escape each URL segment, if necessary, and calculate approximate normalized
  // spec length.
  //
  // [scheme://][username[:password]@]host[:port]/path[?query_string][#ref]

  CheckedInt<uint32_t> approxLen = 0;

  // the scheme is already ASCII
  if (mScheme.mLen > 0) {
    approxLen += int32_t(mScheme.mLen) +
                 3;  // includes room for "://", which we insert always
  }

  // encode URL segments; convert UTF-8 to origin charset and possibly escape.
  // results written to encXXX variables only if |spec| is not already in the
  // appropriate encoding.
  {
    nsSegmentEncoder encoder;
    nsSegmentEncoder queryEncoder(encoding);
    // Username@
    approxLen += encoder.EncodeSegmentCount(spec, mUsername, esc_Username,
                                            encUsername, useEncUsername, 0);
    approxLen += 1;  // reserve length for @
    // :password - we insert the ':' even if there's no actual password if
    // "user:@" was in the spec
    if (mPassword.mLen > 0) {
      approxLen += 1 + encoder.EncodeSegmentCount(spec, mPassword, esc_Password,
                                                  encPassword, useEncPassword);
    }
    // mHost is handled differently below due to encoding differences
    MOZ_ASSERT(mPort >= -1, "Invalid negative mPort");
    if (mPort != -1 && mPort != mDefaultPort) {
      // :port
      portbuf.AppendInt(mPort);
      approxLen += portbuf.Length() + 1;
    }

    approxLen +=
        1;  // reserve space for possible leading '/' - may not be needed
    // Should just use mPath?  These are pessimistic, and thus waste space
    approxLen += encoder.EncodeSegmentCount(spec, mDirectory, esc_Directory,
                                            encDirectory, useEncDirectory, 1);
    approxLen += encoder.EncodeSegmentCount(spec, mBasename, esc_FileBaseName,
                                            encBasename, useEncBasename);
    approxLen += encoder.EncodeSegmentCount(spec, mExtension, esc_FileExtension,
                                            encExtension, useEncExtension, 1);

    // These next ones *always* add their leading character even if length is 0
    // Handles items like "http://#"
    // ?query
    if (mQuery.mLen >= 0) {
      approxLen += 1 + queryEncoder.EncodeSegmentCount(spec, mQuery, esc_Query,
                                                       encQuery, useEncQuery);
    }
    // #ref

    if (mRef.mLen >= 0) {
      approxLen += 1 + encoder.EncodeSegmentCount(spec, mRef, esc_Ref, encRef,
                                                  useEncRef);
    }
  }

  // do not escape the hostname, if IPv6 address literal, mHost will
  // already point to a [ ] delimited IPv6 address literal.
  // However, perform Unicode normalization on it, as IDN does.
  // Note that we don't disallow URLs without a host - file:, etc
  if (mHost.mLen > 0) {
    nsDependentCSubstring tempHost(spec + mHost.mPos, mHost.mLen);
    nsresult rv;
    bool allowIp = !SegmentIs(spec, mScheme, "resource") &&
                   !SegmentIs(spec, mScheme, "moz-src") &&
                   !SegmentIs(spec, mScheme, "chrome");
    if (tempHost.First() == '[' && allowIp) {
      mCheckedIfHostA = true;
      rv = (nsresult)rusturl_parse_ipv6addr(&tempHost, &encHost);
      if (NS_FAILED(rv)) {
        return rv;
      }
    } else {
      rv = NormalizeIDN(tempHost, encHost);
      if (NS_FAILED(rv)) {
        return rv;
      }
      if (IPv4Parser::EndsInANumber(encHost) && allowIp) {
        nsAutoCString ipString;
        rv = IPv4Parser::NormalizeIPv4(encHost, ipString);
        if (NS_FAILED(rv)) {
          return rv;
        }
        encHost = ipString;
      }
    }

    // NormalizeIDN always copies, if the call was successful.
    useEncHost = true;
    approxLen += encHost.Length();
  } else {
    // empty host means empty mDisplayHost
    mDisplayHost.Truncate();
    mCheckedIfHostA = true;
  }

  // We must take a copy of every single segment because they are pointing to
  // the |spec| while we are changing their value, in case we must use
  // encoded strings.
  URLSegment username(mUsername);
  URLSegment password(mPassword);
  URLSegment host(mHost);
  URLSegment path(mPath);
  URLSegment directory(mDirectory);
  URLSegment basename(mBasename);
  URLSegment extension(mExtension);
  URLSegment query(mQuery);
  URLSegment ref(mRef);

  // The encoded string could be longer than the original input, so we need
  // to check the final URI isn't longer than the max length.
  if (!(approxLen + 1).isValid() ||
      approxLen.value() + 1 > StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  //
  // generate the normalized URL string
  //
  // approxLen should be correct or 1 high
  if (!mSpec.SetLength(approxLen.value() + 1,
                       fallible)) {  // buf needs a trailing '\0' below
    return NS_ERROR_OUT_OF_MEMORY;
  }
  char* buf = mSpec.BeginWriting();
  uint32_t i = 0;
  int32_t diff = 0;

  if (mScheme.mLen > 0) {
    i = AppendSegmentToBuf(buf, i, spec, mScheme, mScheme);
    net_ToLowerCase(buf + mScheme.mPos, mScheme.mLen);
    i = AppendToBuf(buf, i, "://", 3);
  }

  // record authority starting position
  mAuthority.mPos = i;

  // append authority
  if (mUsername.mLen > 0 || mPassword.mLen > 0) {
    if (mUsername.mLen > 0) {
      i = AppendSegmentToBuf(buf, i, spec, username, mUsername, &encUsername,
                             useEncUsername, &diff);
      ShiftFromPassword(diff);
    } else {
      mUsername.mLen = -1;
    }
    if (password.mLen > 0) {
      buf[i++] = ':';
      i = AppendSegmentToBuf(buf, i, spec, password, mPassword, &encPassword,
                             useEncPassword, &diff);
      ShiftFromHost(diff);
    } else {
      mPassword.mLen = -1;
    }
    buf[i++] = '@';
  } else {
    mUsername.mLen = -1;
    mPassword.mLen = -1;
  }
  if (host.mLen > 0) {
    i = AppendSegmentToBuf(buf, i, spec, host, mHost, &encHost, useEncHost,
                           &diff);
    ShiftFromPath(diff);

    MOZ_ASSERT(mPort >= -1, "Invalid negative mPort");
    if (mPort != -1 && mPort != mDefaultPort) {
      buf[i++] = ':';
      // Already formatted while building approxLen
      i = AppendToBuf(buf, i, portbuf.get(), portbuf.Length());
    }
  }

  // record authority length
  mAuthority.mLen = i - mAuthority.mPos;

  // path must always start with a "/"
  if (mPath.mLen <= 0) {
    LOG(("setting path=/"));
    mDirectory.mPos = mFilepath.mPos = mPath.mPos = i;
    mDirectory.mLen = mFilepath.mLen = mPath.mLen = 1;
    // basename must exist, even if empty (bug 113508)
    mBasename.mPos = i + 1;
    mBasename.mLen = 0;
    buf[i++] = '/';
  } else {
    uint32_t leadingSlash = 0;
    if (spec[path.mPos] != '/') {
      LOG(("adding leading slash to path\n"));
      leadingSlash = 1;
      buf[i++] = '/';
      // basename must exist, even if empty (bugs 113508, 429347)
      if (mBasename.mLen == -1) {
        mBasename.mPos = basename.mPos = i;
        mBasename.mLen = basename.mLen = 0;
      }
    }

    // record corrected (file)path starting position
    mPath.mPos = mFilepath.mPos = i - leadingSlash;

    // Fast path: nothing in the path region needs escaping, the spec already
    // begins with '/', and the directory already ends with '/'. In that case
    // the slow per-segment loop would write back the spec bytes verbatim, so
    // we can copy the whole [directory][basename][.ext][?query][#ref] block
    // with one memcpy and rebase the segment positions.
    bool pathFastPath = leadingSlash == 0 && !useEncDirectory &&
                        !useEncBasename && !useEncExtension && !useEncQuery &&
                        !useEncRef && directory.mLen > 0 &&
                        spec[directory.mPos + directory.mLen - 1] == '/';
    if (pathFastPath) {
      memcpy(buf + i, spec + path.mPos, path.mLen);
      uint32_t pathStart = i;
      mDirectory.mPos = pathStart + (directory.mPos - path.mPos);
      // mDirectory.mLen unchanged
      mBasename.mPos = pathStart + (basename.mPos - path.mPos);
      // mBasename.mLen unchanged
      if (mExtension.mLen >= 0) {
        mExtension.mPos = pathStart + (extension.mPos - path.mPos);
      }
      if (mQuery.mLen >= 0) {
        mQuery.mPos = pathStart + (query.mPos - path.mPos);
      }
      if (mRef.mLen >= 0) {
        mRef.mPos = pathStart + (ref.mPos - path.mPos);
      }
      int32_t filepathLen = path.mLen;
      if (mQuery.mLen >= 0) {
        filepathLen -= 1 + query.mLen;
      }
      if (mRef.mLen >= 0) {
        filepathLen -= 1 + ref.mLen;
      }
      mFilepath.mLen = filepathLen;
      mPath.mLen = path.mLen;
      i += path.mLen;
    } else {
      i = AppendSegmentToBuf(buf, i, spec, directory, mDirectory, &encDirectory,
                             useEncDirectory, &diff);
      ShiftFromBasename(diff);

      // the directory must end with a '/'
      if (buf[i - 1] != '/') {
        buf[i++] = '/';
        mDirectory.mLen++;
      }

      i = AppendSegmentToBuf(buf, i, spec, basename, mBasename, &encBasename,
                             useEncBasename, &diff);
      ShiftFromExtension(diff);

      // make corrections to directory segment if leadingSlash
      if (leadingSlash) {
        mDirectory.mPos = mPath.mPos;
        if (mDirectory.mLen >= 0) {
          mDirectory.mLen += leadingSlash;
        } else {
          mDirectory.mLen = 1;
        }
      }

      if (mExtension.mLen >= 0) {
        buf[i++] = '.';
        i = AppendSegmentToBuf(buf, i, spec, extension, mExtension,
                               &encExtension, useEncExtension, &diff);
        ShiftFromQuery(diff);
      }
      // calculate corrected filepath length
      mFilepath.mLen = i - mFilepath.mPos;

      if (mQuery.mLen >= 0) {
        buf[i++] = '?';
        i = AppendSegmentToBuf(buf, i, spec, query, mQuery, &encQuery,
                               useEncQuery, &diff);
        ShiftFromRef(diff);
      }
      if (mRef.mLen >= 0) {
        buf[i++] = '#';
        i = AppendSegmentToBuf(buf, i, spec, ref, mRef, &encRef, useEncRef,
                               &diff);
      }
      // calculate corrected path length
      mPath.mLen = i - mPath.mPos;
    }
  }

  buf[i] = '\0';

  // https://url.spec.whatwg.org/#path-state (1.4.1.2)
  // https://url.spec.whatwg.org/#windows-drive-letter
  if (SegmentIs(buf, mScheme, "file")) {
    char* path = &buf[mPath.mPos];
    // To account for cases like file:///w|/m and file:///c|
    if (mPath.mLen >= 3 && path[0] == '/' && IsAsciiAlpha(path[1]) &&
        path[2] == '|' && (mPath.mLen == 3 || path[3] == '/')) {
      buf[mPath.mPos + 2] = ':';
    }
  }

  if (mDirectory.mLen > 0) {
    CoalescePath(buf + mDirectory.mPos);
  }
  // CoalescePath may shorten mPath in place; mPath.mPos + mPath.mLen is the
  // current end of the written spec, avoiding a full strlen walk over `buf`.
  mSpec.SetLength(mPath.mPos + mPath.mLen);
  ResetSpecHash();

  if (MOZ_UNLIKELY(mSpec.Length() > approxLen.value())) {
    nsPrintfCString msg(
        "approxLen:%X, mSpecLen:%zX, scheme (%X,%X), host (%X,%X), path "
        "(%X,%X)",
        approxLen.value(), mSpec.Length(), (uint32_t)mScheme.mPos,
        (int32_t)mScheme.mLen, (uint32_t)mHost.mPos, (int32_t)mHost.mLen,
        (uint32_t)mPath.mPos, (int32_t)mPath.mLen);
    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::URLSegments, msg);
    MOZ_CRASH("nsStandardURL::BuildNormalizedSpec overflowed mSpec");
  }

  MOZ_ASSERT(mSpec.Length() <= StaticPrefs::network_standard_url_max_length(),
             "The spec should never be this long, we missed a check.");

  MOZ_ASSERT(mUsername.mLen != 0 && mPassword.mLen != 0);
  return NS_OK;
}

bool nsStandardURL::SegmentIs(const URLSegment& seg, const char* val,
                              bool ignoreCase) {
  // one or both may be null
  if (!val || mSpec.IsEmpty()) {
    return (!val && (mSpec.IsEmpty() || seg.mLen < 0));
  }
  if (seg.mLen < 0) {
    return false;
  }
  size_t vlen = strlen(val);
  if (static_cast<uint32_t>(seg.mLen) != vlen) {
    return false;
  }
  if (ignoreCase) {
    return !nsCRT::strncasecmp(mSpec.get() + seg.mPos, val, vlen);
  }
  return !strncmp(mSpec.get() + seg.mPos, val, vlen);
}

bool nsStandardURL::SegmentIs(const char* spec, const URLSegment& seg,
                              const char* val, bool ignoreCase) {
  // one or both may be null
  if (!val || !spec) {
    return (!val && (!spec || seg.mLen < 0));
  }
  if (seg.mLen < 0) {
    return false;
  }
  size_t vlen = strlen(val);
  if (static_cast<uint32_t>(seg.mLen) != vlen) {
    return false;
  }
  if (ignoreCase) {
    return !nsCRT::strncasecmp(spec + seg.mPos, val, vlen);
  }
  return !strncmp(spec + seg.mPos, val, vlen);
}

bool nsStandardURL::SegmentIs(const URLSegment& seg1, const char* val,
                              const URLSegment& seg2, bool ignoreCase) {
  if (seg1.mLen != seg2.mLen) {
    return false;
  }
  if (seg1.mLen == -1 || (!val && mSpec.IsEmpty())) {
    return true;  // both are empty
  }
  if (!val) {
    return false;
  }
  if (ignoreCase) {
    return !nsCRT::strncasecmp(mSpec.get() + seg1.mPos, val + seg2.mPos,
                               seg1.mLen);
  }

  return !strncmp(mSpec.get() + seg1.mPos, val + seg2.mPos, seg1.mLen);
}

int32_t nsStandardURL::ReplaceSegment(uint32_t pos, uint32_t len,
                                      const char* val, uint32_t valLen) {
  if (val && valLen) {
    if (len == 0) {
      mSpec.Insert(val, pos, valLen);
    } else {
      mSpec.Replace(pos, len, nsDependentCString(val, valLen));
    }
    return valLen - len;
  }

  // else remove the specified segment
  mSpec.Cut(pos, len);
  return -int32_t(len);
}

int32_t nsStandardURL::ReplaceSegment(uint32_t pos, uint32_t len,
                                      const nsACString& val) {
  if (len == 0) {
    mSpec.Insert(val, pos);
  } else {
    mSpec.Replace(pos, len, val);
  }
  return val.Length() - len;
}

bool nsStandardURL::ExceedsMaxLength(uint32_t aOldLen, uint32_t aNewLen) const {
  return mSpec.Length() - aOldLen + aNewLen >
         StaticPrefs::network_standard_url_max_length();
}

nsresult nsStandardURL::ParseURL(const char* spec, int32_t specLen) {
  if (specLen > (int32_t)StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  // The four ParseX calls stay virtual, but we make one call instead of four,
  // and ParseAll folds the authority/path offsets on plain ints rather than on
  // the parity-tracked segment members.
  URLParseResult r;
  nsresult rv = mParser->ParseAll(spec, specLen, r);
  if (NS_FAILED(rv)) {
    return rv;
  }

  mScheme.mPos = r.schemePos;
  mScheme.mLen = r.schemeLen;
  mAuthority.mPos = r.authorityPos;
  mAuthority.mLen = r.authorityLen;
  mPath.mPos = r.pathPos;
  mPath.mLen = r.pathLen;

#ifdef DEBUG
  if (mScheme.mLen <= 0) {
    printf("spec=%s\n", spec);
    NS_WARNING("malformed url: no scheme");
  }
#endif

  if (r.authorityLen > 0) {
    mUsername.mPos = r.usernamePos;
    mUsername.mLen = r.usernameLen;
    mPassword.mPos = r.passwordPos;
    mPassword.mLen = r.passwordLen;
    mHost.mPos = r.hostPos;
    mHost.mLen = r.hostLen;
    mPort = r.port;
    // Don't allow mPort to be set to this URI's default port
    if (mPort == mDefaultPort) {
      mPort = -1;
    }
  }

  if (r.pathLen > 0) {
    if (r.pathLen > (int32_t)StaticPrefs::network_standard_url_max_length()) {
      return NS_ERROR_MALFORMED_URI;
    }
    mFilepath.mPos = r.filepathPos;
    mFilepath.mLen = r.filepathLen;
    mQuery.mPos = r.queryPos;
    mQuery.mLen = r.queryLen;
    mRef.mPos = r.refPos;
    mRef.mLen = r.refLen;

    if (r.filepathLen > 0) {
      mDirectory.mPos = r.directoryPos;
      mDirectory.mLen = r.directoryLen;
      mBasename.mPos = r.basenamePos;
      mBasename.mLen = r.basenameLen;
      mExtension.mPos = r.extensionPos;
      mExtension.mLen = r.extensionLen;
    }
  }

  return NS_OK;
}

char* nsStandardURL::AppendToSubstring(uint32_t pos, int32_t len,
                                       const char* tail) {
  // Verify pos and length are within boundaries
  if (pos > mSpec.Length()) {
    return nullptr;
  }
  if (len < 0) {
    return nullptr;
  }
  if ((uint32_t)len > (mSpec.Length() - pos)) {
    return nullptr;
  }
  if (!tail) {
    return nullptr;
  }

  uint32_t tailLen = strlen(tail);

  // Check for int overflow for proposed length of combined string
  if (UINT32_MAX - ((uint32_t)len + 1) < tailLen) {
    return nullptr;
  }

  char* result = (char*)moz_xmalloc(len + tailLen + 1);
  memcpy(result, mSpec.get() + pos, len);
  memcpy(result + len, tail, tailLen);
  result[len + tailLen] = '\0';
  return result;
}

nsresult nsStandardURL::ReadSegment(nsIBinaryInputStream* stream,
                                    URLSegment& seg) {
  nsresult rv;

  uint32_t pos = seg.mPos;
  rv = stream->Read32(&pos);
  if (NS_FAILED(rv)) {
    return rv;
  }

  seg.mPos = pos;

  uint32_t len = seg.mLen;
  rv = stream->Read32(&len);
  if (NS_FAILED(rv)) {
    return rv;
  }

  CheckedInt<int32_t> checkedLen(len);
  if (!checkedLen.isValid()) {
    seg.mLen = -1;
  } else {
    seg.mLen = len;
  }

  return NS_OK;
}

nsresult nsStandardURL::WriteSegment(nsIBinaryOutputStream* stream,
                                     const URLSegment& seg) {
  nsresult rv;
  rv = stream->Write32(seg.mPos);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = stream->Write32(uint32_t(seg.mLen));
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

#define SHIFT_FROM(name, what)                         \
  void nsStandardURL::name(int32_t diff) {             \
    if (!diff) return;                                 \
    if ((what).mLen >= 0) {                            \
      CheckedInt<int32_t> pos = (uint32_t)(what).mPos; \
      pos += diff;                                     \
      MOZ_ASSERT(pos.isValid());                       \
      (what).mPos = pos.value();                       \
    } else {                                           \
      MOZ_RELEASE_ASSERT((what).mLen == -1);           \
    }

#define SHIFT_FROM_NEXT(name, what, next) \
  SHIFT_FROM(name, what)                  \
  next(diff);                             \
  }

#define SHIFT_FROM_LAST(name, what) \
  SHIFT_FROM(name, what)            \
  }

SHIFT_FROM_NEXT(ShiftFromAuthority, mAuthority, ShiftFromUsername)
SHIFT_FROM_NEXT(ShiftFromUsername, mUsername, ShiftFromPassword)
SHIFT_FROM_NEXT(ShiftFromPassword, mPassword, ShiftFromHost)
SHIFT_FROM_NEXT(ShiftFromHost, mHost, ShiftFromPath)
SHIFT_FROM_NEXT(ShiftFromPath, mPath, ShiftFromFilepath)
SHIFT_FROM_NEXT(ShiftFromFilepath, mFilepath, ShiftFromDirectory)
SHIFT_FROM_NEXT(ShiftFromDirectory, mDirectory, ShiftFromBasename)
SHIFT_FROM_NEXT(ShiftFromBasename, mBasename, ShiftFromExtension)
SHIFT_FROM_NEXT(ShiftFromExtension, mExtension, ShiftFromQuery)
SHIFT_FROM_NEXT(ShiftFromQuery, mQuery, ShiftFromRef)
SHIFT_FROM_LAST(ShiftFromRef, mRef)

//----------------------------------------------------------------------------
// nsStandardURL::nsIClassInfo
//----------------------------------------------------------------------------

NS_IMPL_CLASSINFO(nsStandardURL, nullptr, nsIClassInfo::THREADSAFE,
                  NS_STANDARDURL_CID)
// Empty CI getter. We only need nsIClassInfo for Serialization
NS_IMPL_CI_INTERFACE_GETTER0(nsStandardURL)

//----------------------------------------------------------------------------
// nsStandardURL::nsISupports
//----------------------------------------------------------------------------

NS_IMPL_ADDREF(nsStandardURL)
NS_IMPL_RELEASE(nsStandardURL)

NS_INTERFACE_MAP_BEGIN(nsStandardURL)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIStandardURL)
  NS_INTERFACE_MAP_ENTRY(nsIURI)
  NS_INTERFACE_MAP_ENTRY(nsIURL)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIFileURL, mSupportsFileURL)
  NS_INTERFACE_MAP_ENTRY(nsIStandardURL)
  NS_INTERFACE_MAP_ENTRY(nsISerializable)
  NS_IMPL_QUERY_CLASSINFO(nsStandardURL)
  NS_INTERFACE_MAP_ENTRY(nsISensitiveInfoHiddenURI)
  NS_INTERFACE_MAP_ENTRY(nsIIPCSerializableURI)
  NS_INTERFACE_MAP_ENTRY(nsIURIWithSizeOf)
  // see nsStandardURL::Equals
  if (aIID.Equals(kThisImplCID)) {
    foundInterface = static_cast<nsIURI*>(this);
  } else
NS_INTERFACE_MAP_END

//----------------------------------------------------------------------------
// nsStandardURL::nsIURI
//----------------------------------------------------------------------------

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetSpec(nsACString& result) {
  MOZ_ASSERT(mSpec.Length() <= StaticPrefs::network_standard_url_max_length(),
             "The spec should never be this long, we missed a check.");
  result = mSpec;
  return NS_OK;
}

uint32_t nsStandardURL::SpecHash() { return CachedSpecHash(mSpec); }

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetSensitiveInfoHiddenSpec(nsACString& result) {
  nsresult rv = GetSpec(result);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (mPassword.mLen > 0) {
    result.ReplaceLiteral(mPassword.mPos, mPassword.mLen, "****");
  }
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetSpecIgnoringRef(nsACString& result) {
  // URI without ref is 0 to one char before ref
  if (mRef.mLen < 0) {
    return GetSpec(result);
  }

  URLSegment noRef(0, mRef.mPos - 1);
  result = Segment(noRef);
  MOZ_ASSERT(mCheckedIfHostA);
  return NS_OK;
}

nsresult nsStandardURL::CheckIfHostIsAscii() {
  nsresult rv;
  if (mCheckedIfHostA) {
    return NS_OK;
  }

  mCheckedIfHostA = true;

  nsAutoCString displayHost;
  // IPC deseriazation can have IPv6 without square brackets here.
  rv = NS_DomainToDisplayAllowAnyGlyphfulASCII(Host(), displayHost);
  if (NS_FAILED(rv)) {
    mDisplayHost.Truncate();
    mCheckedIfHostA = false;
    return rv;
  }

  if (!mozilla::IsAscii(displayHost)) {
    mDisplayHost = std::move(displayHost);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::GetDisplaySpec(nsACString& aUnicodeSpec) {
  aUnicodeSpec.Assign(mSpec);
  MOZ_ASSERT(mCheckedIfHostA);
  if (!mDisplayHost.IsEmpty()) {
    aUnicodeSpec.Replace(mHost.mPos, mHost.mLen, mDisplayHost);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::GetDisplayHostPort(nsACString& aUnicodeHostPort) {
  nsAutoCString unicodeHostPort;

  nsresult rv = GetDisplayHost(unicodeHostPort);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (StringBeginsWith(Hostport(), "["_ns)) {
    aUnicodeHostPort.AssignLiteral("[");
    aUnicodeHostPort.Append(unicodeHostPort);
    aUnicodeHostPort.AppendLiteral("]");
  } else {
    aUnicodeHostPort.Assign(unicodeHostPort);
  }

  uint32_t pos = mHost.mPos + mHost.mLen;
  if (pos < mPath.mPos) {
    aUnicodeHostPort += Substring(mSpec, pos, mPath.mPos - pos);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::GetDisplayHost(nsACString& aUnicodeHost) {
  MOZ_ASSERT(mCheckedIfHostA);
  if (mDisplayHost.IsEmpty()) {
    return GetAsciiHost(aUnicodeHost);
  }

  aUnicodeHost = mDisplayHost;
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetPrePath(nsACString& result) {
  result = Prepath();
  MOZ_ASSERT(mCheckedIfHostA);
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetDisplayPrePath(nsACString& result) {
  result = Prepath();
  MOZ_ASSERT(mCheckedIfHostA);
  if (!mDisplayHost.IsEmpty()) {
    result.Replace(mHost.mPos, mHost.mLen, mDisplayHost);
  }
  return NS_OK;
}

// result is strictly US-ASCII
NS_IMETHODIMP
nsStandardURL::GetScheme(nsACString& result) {
  result = Scheme();
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetUserPass(nsACString& result) {
  result = Userpass();
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetUsername(nsACString& result) {
  result = Username();
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetPassword(nsACString& result) {
  result = Password();
  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::GetHostPort(nsACString& result) {
  return GetAsciiHostPort(result);
}

NS_IMETHODIMP
nsStandardURL::GetHost(nsACString& result) { return GetAsciiHost(result); }

NS_IMETHODIMP
nsStandardURL::GetPort(int32_t* result) {
  // should never be more than 16 bit
  MOZ_ASSERT(mPort <= std::numeric_limits<uint16_t>::max());
  *result = mPort;
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetPathQueryRef(nsACString& result) {
  result = Path();
  return NS_OK;
}

// result is ASCII
NS_IMETHODIMP
nsStandardURL::GetAsciiSpec(nsACString& result) {
  result = mSpec;
  return NS_OK;
}

// result is ASCII
NS_IMETHODIMP
nsStandardURL::GetAsciiHostPort(nsACString& result) {
  result = Hostport();
  return NS_OK;
}

// result is ASCII
NS_IMETHODIMP
nsStandardURL::GetAsciiHost(nsACString& result) {
  result = Host();
  return NS_OK;
}

static bool IsSpecialProtocol(const nsACString& input) {
  const char* start = input.BeginReading();
  const char* end = input.EndReading();
  const char* colon = start;
  while (colon != end && *colon != ':') {
    ++colon;
  }
  // Dispatch on scheme length so each input only compares against schemes of
  // its own length. Uses a dependent substring -- no copy.
  const nsDependentCSubstring scheme(start, colon - start);
  switch (colon - start) {
    case 2:
      return scheme.LowerCaseEqualsLiteral("ws");
    case 3:
      return scheme.LowerCaseEqualsLiteral("ftp") ||
             scheme.LowerCaseEqualsLiteral("wss");
    case 4:
      return scheme.LowerCaseEqualsLiteral("http") ||
             scheme.LowerCaseEqualsLiteral("file");
    case 5:
      return scheme.LowerCaseEqualsLiteral("https");
    case 6:
      return scheme.LowerCaseEqualsLiteral("gopher");
    default:
      return false;
  }
}

nsresult nsStandardURL::SetSpecInternal(const nsACString& input) {
  return SetSpecWithEncoding(input, nullptr);
}

nsresult nsStandardURL::SetSpecWithEncoding(const nsACString& input,
                                            const Encoding* encoding) {
  const nsPromiseFlatCString& flat = PromiseFlatCString(input);
  LOG(("nsStandardURL::SetSpec [spec=%s]\n", flat.get()));

  if (input.Length() > StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  // filter out unexpected chars "\r\n\t" if necessary
  nsAutoCString filteredURI;
  net_FilterURIString(flat, filteredURI);

  if (filteredURI.Length() == 0) {
    return NS_ERROR_MALFORMED_URI;
  }

  Clear();

  if (IsSpecialProtocol(filteredURI)) {
    // Bug 652186: Replace all backslashes with slashes when parsing paths
    // Stop when we reach the query or the hash.
    // Pre-scan read-only first so we don't force a BeginWriting() copy of a
    // shared string when there is nothing to rewrite (the common case).
    const char* readStart = filteredURI.BeginReading();
    const char* readEnd = filteredURI.EndReading();
    const char* firstBackslash = nullptr;
    for (const char* p = readStart; p != readEnd; ++p) {
      if (*p == '?' || *p == '#') {
        break;
      }
      if (*p == '\\') {
        firstBackslash = p;
        break;
      }
    }
    if (firstBackslash) {
      size_t offset = firstBackslash - readStart;
      char* start = filteredURI.BeginWriting() + offset;
      char* end = filteredURI.EndWriting();
      while (start != end) {
        if (*start == '?' || *start == '#') {
          break;
        }
        if (*start == '\\') {
          *start = '/';
        }
        start++;
      }
    }
  }

  const char* spec = filteredURI.get();
  int32_t specLength = filteredURI.Length();

  // parse the given URL...
  nsresult rv = ParseURL(spec, specLength);
  if (mScheme.mLen <= 0) {
    rv = NS_ERROR_MALFORMED_URI;
  }
  if (NS_SUCCEEDED(rv)) {
    // finally, use the URLSegment member variables to build a normalized
    // copy of |spec|
    rv = BuildNormalizedSpec(spec, encoding);
  }

  // Make sure that a URLTYPE_AUTHORITY has a non-empty hostname.
  if (mURLType == URLTYPE_AUTHORITY && mHost.mLen <= 0) {
    rv = NS_ERROR_MALFORMED_URI;
  }

  if (NS_FAILED(rv)) {
    // A late failure leaves the segments describing |spec| while mSpec is
    // still empty, which callers' SanityCheck() would treat as out of range.
    Clear();
    return rv;
  }

  if (LOG_ENABLED()) {
    LOG((" spec      = %s\n", mSpec.get()));
    LOG((" port      = %d\n", mPort));
    LOG((" scheme    = (%u,%d)\n", (uint32_t)mScheme.mPos,
         (int32_t)mScheme.mLen));
    LOG((" authority = (%u,%d)\n", (uint32_t)mAuthority.mPos,
         (int32_t)mAuthority.mLen));
    LOG((" username  = (%u,%d)\n", (uint32_t)mUsername.mPos,
         (int32_t)mUsername.mLen));
    LOG((" password  = (%u,%d)\n", (uint32_t)mPassword.mPos,
         (int32_t)mPassword.mLen));
    LOG((" hostname  = (%u,%d)\n", (uint32_t)mHost.mPos, (int32_t)mHost.mLen));
    LOG((" path      = (%u,%d)\n", (uint32_t)mPath.mPos, (int32_t)mPath.mLen));
    LOG((" filepath  = (%u,%d)\n", (uint32_t)mFilepath.mPos,
         (int32_t)mFilepath.mLen));
    LOG((" directory = (%u,%d)\n", (uint32_t)mDirectory.mPos,
         (int32_t)mDirectory.mLen));
    LOG((" basename  = (%u,%d)\n", (uint32_t)mBasename.mPos,
         (int32_t)mBasename.mLen));
    LOG((" extension = (%u,%d)\n", (uint32_t)mExtension.mPos,
         (int32_t)mExtension.mLen));
    LOG((" query     = (%u,%d)\n", (uint32_t)mQuery.mPos,
         (int32_t)mQuery.mLen));
    LOG((" ref       = (%u,%d)\n", (uint32_t)mRef.mPos, (int32_t)mRef.mLen));
  }

  SanityCheck();
  return rv;
}

nsresult nsStandardURL::SetScheme(const nsACString& input) {
  // Strip tabs, newlines, carriage returns from input
  nsAutoCString scheme(input);
  scheme.StripTaggedASCII(ASCIIMask::MaskCRLFTab());

  LOG(("nsStandardURL::SetScheme [scheme=%s]\n", scheme.get()));

  if (scheme.IsEmpty()) {
    NS_WARNING("cannot remove the scheme from an url");
    return NS_ERROR_UNEXPECTED;
  }
  if (mScheme.mLen < 0) {
    NS_WARNING("uninitialized");
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!net_IsValidScheme(scheme)) {
    NS_WARNING("the given url scheme contains invalid characters");
    return NS_ERROR_UNEXPECTED;
  }

  if (mSpec.Length() + input.Length() - Scheme().Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  InvalidateCache();

  int32_t shift = ReplaceSegment(mScheme.mPos, mScheme.mLen, scheme);

  if (shift) {
    mScheme.mLen = scheme.Length();
    ShiftFromAuthority(shift);
  }

  // ensure new scheme is lowercase
  //
  // XXX the string code unfortunately doesn't provide a ToLowerCase
  //     that operates on a substring.
  net_ToLowerCase((char*)mSpec.get(), mScheme.mLen);

  // If the scheme changes the default port also changes.
  if (Scheme() == "http"_ns || Scheme() == "ws"_ns) {
    mDefaultPort = 80;
  } else if (Scheme() == "https"_ns || Scheme() == "wss"_ns) {
    mDefaultPort = 443;
  }
  if (mPort == mDefaultPort && mAuthority.mLen >= 0) {
    nsresult rv = SetPort(-1);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult nsStandardURL::SetUserPass(const nsACString& input) {
  const nsPromiseFlatCString& userpass = PromiseFlatCString(input);

  LOG(("nsStandardURL::SetUserPass [userpass=%s]\n", userpass.get()));

  if (mURLType == URLTYPE_NO_AUTHORITY) {
    if (userpass.IsEmpty()) {
      return NS_OK;
    }
    NS_WARNING("cannot set user:pass on no-auth url");
    return NS_ERROR_UNEXPECTED;
  }
  if (mAuthority.mLen < 0) {
    NS_WARNING("uninitialized");
    return NS_ERROR_NOT_INITIALIZED;
  }
  if (mAuthority.mLen == 0) {
    // If the URL doesn't have a hostname then setting the userpass to
    // empty string is a no-op. But setting it to anything else should
    // return an error.
    if (input.Length() == 0) {
      return NS_OK;
    } else {
      return NS_ERROR_UNEXPECTED;
    }
  }

  if (mSpec.Length() + input.Length() - Userpass(true).Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });
  InvalidateCache();

  NS_ASSERTION(mHost.mLen >= 0, "uninitialized");

  nsresult rv;
  uint32_t usernamePos, passwordPos;
  int32_t usernameLen, passwordLen;

  rv = mParser->ParseUserInfo(userpass.get(), userpass.Length(), &usernamePos,
                              &usernameLen, &passwordPos, &passwordLen);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // build new user:pass in |buf|
  nsAutoCString buf;
  if (usernameLen > 0 || passwordLen > 0) {
    nsSegmentEncoder encoder;
    bool ignoredOut;
    usernameLen = encoder.EncodeSegmentCount(
        userpass.get(), URLSegment(usernamePos, usernameLen),
        esc_Username | esc_AlwaysCopy, buf, ignoredOut);
    if (passwordLen > 0) {
      buf.Append(':');
      passwordLen = encoder.EncodeSegmentCount(
          userpass.get(), URLSegment(passwordPos, passwordLen),
          esc_Password | esc_AlwaysCopy, buf, ignoredOut);
    } else {
      passwordLen = -1;
    }
    if (mUsername.mLen < 0 && mPassword.mLen < 0) {
      buf.Append('@');
    }
  }

  int32_t shift = 0;

  if (mUsername.mLen < 0 && mPassword.mLen < 0) {
    // no existing user:pass
    if (!buf.IsEmpty()) {
      if (ExceedsMaxLength(0, buf.Length())) {
        return NS_ERROR_MALFORMED_URI;
      }
      mSpec.Insert(buf, mHost.mPos);
      mUsername.mPos = mHost.mPos;
      shift = buf.Length();
    }
  } else {
    // replace existing user:pass
    uint32_t userpassLen = 0;
    if (mUsername.mLen > 0) {
      userpassLen += mUsername.mLen;
    }
    if (mPassword.mLen > 0) {
      userpassLen += (mPassword.mLen + 1);
    }
    if (buf.IsEmpty()) {
      // remove `@` character too
      userpassLen++;
    }
    if (ExceedsMaxLength(userpassLen, buf.Length())) {
      return NS_ERROR_MALFORMED_URI;
    }
    mSpec.Replace(mAuthority.mPos, userpassLen, buf);
    shift = buf.Length() - userpassLen;
  }
  if (shift) {
    ShiftFromHost(shift);
    MOZ_DIAGNOSTIC_ASSERT(mAuthority.mLen >= -shift);
    mAuthority.mLen += shift;
  }
  // update positions and lengths
  mUsername.mLen = usernameLen > 0 ? usernameLen : -1;
  mUsername.mPos = mAuthority.mPos;
  mPassword.mLen = passwordLen > 0 ? passwordLen : -1;
  if (passwordLen > 0) {
    if (mUsername.mLen > 0) {
      mPassword.mPos = mUsername.mPos + mUsername.mLen + 1;
    } else {
      mPassword.mPos = mAuthority.mPos + 1;
    }
  }

  MOZ_ASSERT(mUsername.mLen != 0 && mPassword.mLen != 0);
  return NS_OK;
}

nsresult nsStandardURL::SetUsername(const nsACString& input) {
  const nsPromiseFlatCString& username = PromiseFlatCString(input);

  LOG(("nsStandardURL::SetUsername [username=%s]\n", username.get()));

  if (mURLType == URLTYPE_NO_AUTHORITY) {
    if (username.IsEmpty()) {
      return NS_OK;
    }
    NS_WARNING("cannot set username on no-auth url");
    return NS_ERROR_UNEXPECTED;
  }
  if (mAuthority.mLen == 0) {
    // If the URL doesn't have a hostname then setting the username to
    // empty string is a no-op. But setting it to anything else should
    // return an error.
    if (input.Length() == 0) {
      return NS_OK;
    } else {
      return NS_ERROR_UNEXPECTED;
    }
  }

  if (mSpec.Length() + input.Length() - Username().Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  InvalidateCache();

  // escape username if necessary
  nsAutoCString buf;
  nsSegmentEncoder encoder;
  const nsACString& escUsername =
      encoder.EncodeSegment(username, esc_Username, buf);

  int32_t shift = 0;

  if (mUsername.mLen < 0 && escUsername.IsEmpty()) {
    return NS_OK;
  }

  if (mUsername.mLen < 0 && mPassword.mLen < 0) {
    MOZ_ASSERT(!escUsername.IsEmpty(), "Should not be empty at this point");
    if (ExceedsMaxLength(0, escUsername.Length() + 1)) {
      return NS_ERROR_MALFORMED_URI;
    }
    mUsername.mPos = mAuthority.mPos;
    mSpec.Insert(escUsername + "@"_ns, mUsername.mPos);
    shift = escUsername.Length() + 1;
    mUsername.mLen = escUsername.Length() > 0 ? escUsername.Length() : -1;
  } else {
    uint32_t pos = mUsername.mLen < 0 ? mAuthority.mPos : mUsername.mPos;
    int32_t len = mUsername.mLen < 0 ? 0 : mUsername.mLen;

    if (mPassword.mLen < 0 && escUsername.IsEmpty()) {
      len++;  // remove the @ character too
    }
    if (ExceedsMaxLength(len, escUsername.Length())) {
      return NS_ERROR_MALFORMED_URI;
    }
    shift = ReplaceSegment(pos, len, escUsername);
    mUsername.mLen = escUsername.Length() > 0 ? escUsername.Length() : -1;
    mUsername.mPos = pos;
  }

  if (shift) {
    mAuthority.mLen += shift;
    ShiftFromPassword(shift);
  }

  MOZ_ASSERT(mUsername.mLen != 0 && mPassword.mLen != 0);
  return NS_OK;
}

nsresult nsStandardURL::SetPassword(const nsACString& input) {
  const nsPromiseFlatCString& password = PromiseFlatCString(input);

  auto clearedPassword = MakeScopeExit([&password, this]() {
    // Check that if this method is called with the empty string then the
    // password is definitely cleared when exiting this method.
    if (password.IsEmpty()) {
      MOZ_DIAGNOSTIC_ASSERT(this->Password().IsEmpty());
    }
    (void)this;  // silence compiler -Wunused-lambda-capture
  });

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  LOG(("nsStandardURL::SetPassword [password=%s]\n", password.get()));

  if (mURLType == URLTYPE_NO_AUTHORITY) {
    if (password.IsEmpty()) {
      return NS_OK;
    }
    NS_WARNING("cannot set password on no-auth url");
    return NS_ERROR_UNEXPECTED;
  }
  if (mAuthority.mLen == 0) {
    // If the URL doesn't have a hostname then setting the password to
    // empty string is a no-op. But setting it to anything else should
    // return an error.
    if (input.Length() == 0) {
      return NS_OK;
    } else {
      return NS_ERROR_UNEXPECTED;
    }
  }

  if (mSpec.Length() + input.Length() - Password().Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  InvalidateCache();

  if (password.IsEmpty()) {
    if (mPassword.mLen > 0) {
      // cut(":password")
      int32_t len = mPassword.mLen;
      if (mUsername.mLen < 0) {
        len++;  // also cut the @ character
      }
      len++;  // for the : character
      mSpec.Cut(mPassword.mPos - 1, len);
      ShiftFromHost(-len);
      mAuthority.mLen -= len;
      mPassword.mLen = -1;
    }
    MOZ_ASSERT(mUsername.mLen != 0 && mPassword.mLen != 0);
    return NS_OK;
  }

  // escape password if necessary
  nsAutoCString buf;
  nsSegmentEncoder encoder;
  const nsACString& escPassword =
      encoder.EncodeSegment(password, esc_Password, buf);

  int32_t shift;

  if (mPassword.mLen < 0) {
    if (mUsername.mLen > 0) {
      if (ExceedsMaxLength(0, escPassword.Length() + 1)) {
        return NS_ERROR_MALFORMED_URI;
      }
      mPassword.mPos = mUsername.mPos + mUsername.mLen + 1;
      mSpec.Insert(":"_ns + escPassword, mPassword.mPos - 1);
      shift = escPassword.Length() + 1;
    } else {
      if (ExceedsMaxLength(0, escPassword.Length() + 2)) {
        return NS_ERROR_MALFORMED_URI;
      }
      mPassword.mPos = mAuthority.mPos + 1;
      mSpec.Insert(":"_ns + escPassword + "@"_ns, mPassword.mPos - 1);
      shift = escPassword.Length() + 2;
    }
  } else {
    if (ExceedsMaxLength(mPassword.mLen, escPassword.Length())) {
      return NS_ERROR_MALFORMED_URI;
    }
    shift = ReplaceSegment(mPassword.mPos, mPassword.mLen, escPassword);
  }

  if (shift) {
    mPassword.mLen = escPassword.Length();
    mAuthority.mLen += shift;
    ShiftFromHost(shift);
  }

  MOZ_ASSERT(mUsername.mLen != 0 && mPassword.mLen != 0);
  return NS_OK;
}

void nsStandardURL::FindHostLimit(nsACString::const_iterator& aStart,
                                  nsACString::const_iterator& aEnd) {
  for (int32_t i = 0; gHostLimitDigits[i]; ++i) {
    nsACString::const_iterator c(aStart);
    if (FindCharInReadable(gHostLimitDigits[i], c, aEnd)) {
      aEnd = c;
    }
  }
}

// If aValue only has a host part and no port number, the port
// will not be reset!!!
nsresult nsStandardURL::SetHostPort(const nsACString& aValue) {
  // We cannot simply call nsIURI::SetHost because that would treat the name as
  // an IPv6 address (like http:://[server:443]/).  We also cannot call
  // nsIURI::SetHostPort because that isn't implemented.  Sadfaces.

  nsACString::const_iterator start, end;
  aValue.BeginReading(start);
  aValue.EndReading(end);
  nsACString::const_iterator iter(start);
  bool isIPv6 = false;

  FindHostLimit(start, end);

  if (*start == '[') {  // IPv6 address
    if (!FindCharInReadable(']', iter, end)) {
      // the ] character is missing
      return NS_ERROR_MALFORMED_URI;
    }
    // iter now at the ']' character
    isIPv6 = true;
  } else {
    nsACString::const_iterator iter2(start);
    if (FindCharInReadable(']', iter2, end)) {
      // if the first char isn't [ then there should be no ] character
      return NS_ERROR_MALFORMED_URI;
    }
  }

  FindCharInReadable(':', iter, end);

  if (!isIPv6 && iter != end) {
    nsACString::const_iterator iter2(iter);
    iter2++;  // Skip over the first ':' character
    if (FindCharInReadable(':', iter2, end)) {
      // If there is more than one ':' character it suggests an IPv6
      // The format should be [2001::1]:80 where the port is optional
      return NS_ERROR_MALFORMED_URI;
    }
  }

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  nsresult rv = SetHost(Substring(start, iter));
  NS_ENSURE_SUCCESS(rv, rv);

  if (iter == end) {
    // does not end in colon
    return NS_OK;
  }

  iter++;  // advance over the colon
  if (iter == end) {
    // port number is missing
    return NS_OK;
  }

  nsCString portStr(Substring(iter, end));
  int32_t port = portStr.ToInteger(&rv);
  if (NS_FAILED(rv)) {
    // Failure parsing the port number
    return NS_OK;
  }

  (void)SetPort(port);
  return NS_OK;
}

nsresult nsStandardURL::SetHost(const nsACString& input) {
  nsAutoCString hostname(input);
  hostname.StripTaggedASCII(ASCIIMask::MaskCRLFTab());

  LOG(("nsStandardURL::SetHost [host=%s]\n", hostname.get()));

  nsACString::const_iterator start, end;
  hostname.BeginReading(start);
  hostname.EndReading(end);

  FindHostLimit(start, end);

  nsDependentCSubstring flat(start, end);

  if (mURLType == URLTYPE_NO_AUTHORITY) {
    if (flat.IsEmpty()) {
      return NS_OK;
    }
    NS_WARNING("cannot set host on no-auth url");
    return NS_ERROR_UNEXPECTED;
  }

  if (mURLType == URLTYPE_AUTHORITY && flat.IsEmpty()) {
    // Setting an empty hostname is not allowed for URLTYPE_AUTHORITY.
    return NS_ERROR_UNEXPECTED;
  }

  if (mSpec.Length() + flat.Length() - Host().Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });
  InvalidateCache();

  uint32_t len;
  nsAutoCString hostBuf;
  nsresult rv;
  bool allowIp =
      !SegmentIs(mScheme, "resource") && !SegmentIs(mScheme, "chrome");
  if (!flat.IsEmpty() && flat.First() == '[' && allowIp) {
    mCheckedIfHostA = true;
    rv = rusturl_parse_ipv6addr(&flat, &hostBuf);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    rv = NormalizeIDN(flat, hostBuf);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (IPv4Parser::EndsInANumber(hostBuf) && allowIp) {
      nsAutoCString ipString;
      rv = IPv4Parser::NormalizeIPv4(hostBuf, ipString);
      if (NS_FAILED(rv)) {
        return rv;
      }
      hostBuf = std::move(ipString);
    }
  }

  // NormalizeIDN always copies if the call was successful
  len = hostBuf.Length();

  if (!len && (mURLType == URLTYPE_AUTHORITY || mPort != -1 ||
               Userpass(true).Length() > 0)) {
    return NS_ERROR_MALFORMED_URI;
  }

  if (ExceedsMaxLength(mHost.mLen < 0 ? 0 : mHost.mLen, len)) {
    return NS_ERROR_MALFORMED_URI;
  }

  if (mHost.mLen < 0) {
    int port_length = 0;
    if (mPort != -1) {
      nsAutoCString buf;
      buf.Assign(':');
      buf.AppendInt(mPort);
      port_length = buf.Length();
    }
    if (mAuthority.mLen > 0) {
      mHost.mPos = mAuthority.mPos + mAuthority.mLen - port_length;
      mHost.mLen = 0;
    } else if (mScheme.mLen > 0) {
      mHost.mPos = mScheme.mPos + mScheme.mLen + 3;
      mHost.mLen = 0;
    }
  }

  int32_t shift = ReplaceSegment(mHost.mPos, mHost.mLen, hostBuf.get(), len);

  if (shift) {
    mHost.mLen = len;
    mAuthority.mLen += shift;
    ShiftFromPath(shift);
  }

  return NS_OK;
}

nsresult nsStandardURL::SetPort(int32_t port) {
  LOG(("nsStandardURL::SetPort [port=%d]\n", port));

  if ((port == mPort) || (mPort == -1 && port == mDefaultPort)) {
    return NS_OK;
  }

  // ports must be >= 0 and 16 bit
  // -1 == use default
  if (port < -1 || port > std::numeric_limits<uint16_t>::max()) {
    return NS_ERROR_MALFORMED_URI;
  }

  if (mURLType == URLTYPE_NO_AUTHORITY) {
    NS_WARNING("cannot set port on no-auth url");
    return NS_ERROR_UNEXPECTED;
  }
  if (mAuthority.mLen < 0) {
    NS_WARNING("uninitialized");
    return NS_ERROR_NOT_INITIALIZED;
  }
  if (mAuthority.mLen == 0) {
    // If the URL doesn't have a hostname then setting the port to
    // -1 is a no-op. But setting it to anything else should
    // return an error.
    if (port == -1) {
      return NS_OK;
    } else {
      return NS_ERROR_UNEXPECTED;
    }
  }

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  InvalidateCache();
  if (port == mDefaultPort) {
    port = -1;
  }

  if (!ReplacePortInSpec(port)) {
    return NS_ERROR_MALFORMED_URI;
  }

  mPort = port;
  return NS_OK;
}

/**
 * Replaces the existing port in mSpec with aNewPort.
 *
 * The caller is responsible for:
 *  - Calling InvalidateCache (since our mSpec is changing).
 *  - Checking whether aNewPort is mDefaultPort (in which case the
 *    caller should pass aNewPort=-1).
 *
 * Returns false, leaving mSpec unchanged, if the new spec would exceed
 * network.standard-url.max-length.
 */
bool nsStandardURL::ReplacePortInSpec(int32_t aNewPort) {
  NS_ASSERTION(aNewPort != mDefaultPort || mDefaultPort == -1,
               "Caller should check its passed-in value and pass -1 instead of "
               "mDefaultPort, to avoid encoding default port into mSpec");

  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  // Create the (possibly empty) string that we're planning to replace:
  nsAutoCString buf;
  if (mPort != -1) {
    buf.Assign(':');
    buf.AppendInt(mPort);
  }
  // Find the position & length of that string:
  const uint32_t replacedLen = buf.Length();
  const uint32_t replacedStart =
      mAuthority.mPos + mAuthority.mLen - replacedLen;

  // Create the (possibly empty) replacement string:
  if (aNewPort == -1) {
    buf.Truncate();
  } else {
    buf.Assign(':');
    buf.AppendInt(aNewPort);
  }
  if (ExceedsMaxLength(replacedLen, buf.Length())) {
    return false;
  }
  // Perform the replacement:
  mSpec.Replace(replacedStart, replacedLen, buf);

  // Bookkeeping to reflect the new length:
  int32_t shift = buf.Length() - replacedLen;
  mAuthority.mLen += shift;
  ShiftFromPath(shift);
  return true;
}

nsresult nsStandardURL::SetPathQueryRef(const nsACString& input) {
  const nsPromiseFlatCString& path = PromiseFlatCString(input);
  LOG(("nsStandardURL::SetPathQueryRef [path=%s]\n", path.get()));
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  InvalidateCache();

  if (!path.IsEmpty()) {
    nsAutoCString spec;

    spec.Assign(mSpec.get(), mPath.mPos);
    if (path.First() != '/') {
      spec.Append('/');
    }
    spec.Append(path);

    return SetSpecInternal(spec);
  }
  if (mPath.mLen >= 1) {
    mSpec.Cut(mPath.mPos + 1, mPath.mLen - 1);
    // these contain only a '/'
    mPath.mLen = 1;
    mDirectory.mLen = 1;
    mFilepath.mLen = 1;
    // these are no longer defined
    mBasename.mLen = -1;
    mExtension.mLen = -1;
    mQuery.mLen = -1;
    mRef.mLen = -1;
  }
  return NS_OK;
}

// When updating this also update SubstitutingURL::Mutator
// Queries this list of interfaces. If none match, it queries mURI.
NS_IMPL_NSIURIMUTATOR_ISUPPORTS(nsStandardURL::Mutator, nsIURISetters,
                                nsIURIMutator, nsIStandardURLMutator,
                                nsIURLMutator, nsIFileURLMutator,
                                nsISerializable)

NS_IMETHODIMP
nsStandardURL::Mutate(nsIURIMutator** aMutator) {
  RefPtr<nsStandardURL::Mutator> mutator = new nsStandardURL::Mutator();
  nsresult rv = mutator->InitFromURI(this);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mutator.forget(aMutator);
  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::Equals(nsIURI* unknownOther, bool* result) {
  return EqualsInternal(unknownOther, eHonorRef, result);
}

NS_IMETHODIMP
nsStandardURL::EqualsExceptRef(nsIURI* unknownOther, bool* result) {
  return EqualsInternal(unknownOther, eIgnoreRef, result);
}

nsresult nsStandardURL::EqualsInternal(
    nsIURI* unknownOther, nsStandardURL::RefHandlingEnum refHandlingMode,
    bool* result) {
  NS_ENSURE_ARG_POINTER(unknownOther);
  MOZ_ASSERT(result, "null pointer");

  RefPtr<nsStandardURL> other;
  nsresult rv =
      unknownOther->QueryInterface(kThisImplCID, getter_AddRefs(other));
  if (NS_FAILED(rv)) {
    *result = false;
    return NS_OK;
  }

  // First, check whether one URIs is an nsIFileURL while the other
  // is not.  If that's the case, they're different.
  if (mSupportsFileURL != other->mSupportsFileURL) {
    *result = false;
    return NS_OK;
  }

  // Next check parts of a URI that, if different, automatically make the
  // URIs different
  if (!SegmentIs(mScheme, other->mSpec.get(), other->mScheme) ||
      // Check for host manually, since conversion to file will
      // ignore the host!
      !SegmentIs(mHost, other->mSpec.get(), other->mHost) ||
      !SegmentIs(mQuery, other->mSpec.get(), other->mQuery) ||
      !SegmentIs(mUsername, other->mSpec.get(), other->mUsername) ||
      !SegmentIs(mPassword, other->mSpec.get(), other->mPassword) ||
      Port() != other->Port()) {
    // No need to compare files or other URI parts -- these are different
    // beasties
    *result = false;
    return NS_OK;
  }

  if (refHandlingMode == eHonorRef &&
      !SegmentIs(mRef, other->mSpec.get(), other->mRef)) {
    *result = false;
    return NS_OK;
  }

  // Then check for exact identity of URIs.  If we have it, they're equal
  if (SegmentIs(mDirectory, other->mSpec.get(), other->mDirectory) &&
      SegmentIs(mBasename, other->mSpec.get(), other->mBasename) &&
      SegmentIs(mExtension, other->mSpec.get(), other->mExtension)) {
    *result = true;
    return NS_OK;
  }

  // At this point, the URIs are not identical, but they only differ in the
  // directory/filename/extension.  If these are file URLs, then get the
  // corresponding file objects and compare those, since two filenames that
  // differ, eg, only in case could still be equal.
  if (mSupportsFileURL) {
    // Assume not equal for failure cases... but failures in GetFile are
    // really failures, more or less, so propagate them to caller.
    *result = false;

    nsCOMPtr<nsIFile> file, otherFile;
    rv = ResolveFile(getter_AddRefs(file));
    nsresult rv2 = other->ResolveFile(getter_AddRefs(otherFile));

    // Special case for resource:// urls that don't resolve to files,
    // and for moz-extension://UUID/_generated_background_page.html
    // because it doesn't resolve to a file (instead it resolves to a data: URI,
    // see ExtensionProtocolHandler::ResolveSpecialCases, see Bug 1926106).
    if (rv == NS_ERROR_NO_INTERFACE || rv2 == NS_ERROR_NO_INTERFACE) {
      return NS_OK;
    }

    if (NS_FAILED(rv)) {
      LOG(("nsStandardURL::Equals [this=%p spec=%s] failed to ensure file",
           this, mSpec.get()));
      return rv;
    }

    rv = rv2;
    if (NS_FAILED(rv)) {
      LOG(
          ("nsStandardURL::Equals [other=%p spec=%s] other failed to ensure "
           "file",
           other.get(), other->mSpec.get()));
      return rv;
    }
    return file->Equals(otherFile, result);
  }

  // The URLs are not identical, and they do not correspond to the
  // same file, so they are different.
  *result = false;

  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::SchemeIs(const char* scheme, bool* result) {
  MOZ_ASSERT(result, "null pointer");
  if (!scheme) {
    *result = false;
    return NS_OK;
  }

  *result = SegmentIs(mScheme, scheme);
  return NS_OK;
}

/* virtual */ nsStandardURL* nsStandardURL::StartClone() {
  nsStandardURL* clone = new nsStandardURL();
  return clone;
}

nsresult nsStandardURL::Clone(nsIURI** aURI) {
  return CloneInternal(eHonorRef, ""_ns, aURI);
}

nsresult nsStandardURL::CloneInternal(
    nsStandardURL::RefHandlingEnum aRefHandlingMode, const nsACString& aNewRef,
    nsIURI** aClone)

{
  RefPtr<nsStandardURL> clone = StartClone();
  if (!clone) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Copy local members into clone.
  // Also copies the cached members mFile, mDisplayHost
  clone->CopyMembers(this, aRefHandlingMode, aNewRef, true);

  clone.forget(aClone);
  return NS_OK;
}

nsresult nsStandardURL::CopyMembers(
    nsStandardURL* source, nsStandardURL::RefHandlingEnum refHandlingMode,
    const nsACString& newRef, bool copyCached) {
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  mSpec = source->mSpec;
  mDefaultPort = source->mDefaultPort;
  mPort = source->mPort;
  mScheme = source->mScheme;
  mAuthority = source->mAuthority;
  mUsername = source->mUsername;
  mPassword = source->mPassword;
  mHost = source->mHost;
  mPath = source->mPath;
  mFilepath = source->mFilepath;
  mDirectory = source->mDirectory;
  mBasename = source->mBasename;
  mExtension = source->mExtension;
  mQuery = source->mQuery;
  mRef = source->mRef;
  mURLType = source->mURLType;
  mParser = source->mParser;
  mSupportsFileURL = source->mSupportsFileURL;
  mCheckedIfHostA = source->mCheckedIfHostA;
  mDisplayHost = source->mDisplayHost;

  if (copyCached) {
    mFile = source->mFile;
  } else {
    InvalidateCache(true);
  }

  if (refHandlingMode == eIgnoreRef) {
    SetRef(""_ns);
  } else if (refHandlingMode == eReplaceRef) {
    SetRef(newRef);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::Resolve(const nsACString& in, nsACString& out) {
  const nsPromiseFlatCString& flat = PromiseFlatCString(in);
  // filter out unexpected chars "\r\n\t" if necessary
  nsAutoCString buf;
  net_FilterURIString(flat, buf);

  const char* relpath = buf.get();
  int32_t relpathLen = buf.Length();

  char* result = nullptr;

  LOG(("nsStandardURL::Resolve [this=%p spec=%s relpath=%s]\n", this,
       mSpec.get(), relpath));

  NS_ASSERTION(mParser, "no parser: unitialized");

  // NOTE: there is no need for this function to produce normalized
  // output.  normalization will occur when the result is used to
  // initialize a nsStandardURL object.

  if (mScheme.mLen < 0) {
    NS_WARNING("unable to Resolve URL: this URL not initialized");
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv;
  URLSegment scheme;
  char* resultPath = nullptr;
  bool relative = false;
  uint32_t offset = 0;

  nsAutoCString baseProtocol(Scheme());
  nsAutoCString protocol;
  rv = net_ExtractURLScheme(buf, protocol);

  // Normally, if we parse a scheme, then it's an absolute URI. But because
  // we still support a deprecated form of relative URIs such as: http:file or
  // http:/path/file we can't do that for all protocols.
  // So we just make sure that if there a protocol, it's the same as the
  // current one, otherwise we treat it as an absolute URI.
  if (NS_SUCCEEDED(rv) && protocol != baseProtocol) {
    out = buf;
    return NS_OK;
  }

  // relative urls should never contain a host, so we always want to use
  // the noauth url parser.
  // use it to extract a possible scheme
  uint32_t schemePos = scheme.mPos;
  int32_t schemeLen = scheme.mLen;
  rv = mParser->ParseURL(relpath, relpathLen, &schemePos, &schemeLen, nullptr,
                         nullptr, nullptr, nullptr);

  // if the parser fails (for example because there is no valid scheme)
  // reset the scheme and assume a relative url
  if (NS_FAILED(rv)) {
    scheme.Reset();
  }

  scheme.mPos = schemePos;
  scheme.mLen = schemeLen;

  // Bug 1873976: For cases involving file:c: against file:
  if (NS_SUCCEEDED(rv) && protocol == "file"_ns && baseProtocol == "file"_ns) {
    const char* path = buf.get() + scheme.mPos + scheme.mLen;
    // For instance: file:c:\foo\bar.html against file:///tmp/mock/path
    if (path[0] == ':' && IsAsciiAlpha(path[1]) &&
        (path[2] == ':' || path[2] == '|')) {
      out = buf;
      return NS_OK;
    }
  }

  protocol.Assign(Segment(scheme));

  // We need to do backslash replacement for the following cases:
  // 1. The input is an absolute path with a http/https/ftp scheme
  // 2. The input is a relative path, and the base URL has a http/https/ftp
  // scheme
  if ((protocol.IsEmpty() && IsSpecialProtocol(baseProtocol)) ||
      IsSpecialProtocol(protocol)) {
    auto* start = buf.BeginWriting();
    auto* end = buf.EndWriting();
    while (start != end) {
      if (*start == '?' || *start == '#') {
        break;
      }
      if (*start == '\\') {
        *start = '/';
      }
      start++;
    }
  }

  if (scheme.mLen >= 0) {
    // this URL appears to be absolute
    // but try to find out more
    if (SegmentIs(mScheme, relpath, scheme, true)) {
      // mScheme and Scheme are the same
      // but this can still be relative
      if (strncmp(relpath + scheme.mPos + scheme.mLen, "://", 3) == 0) {
        // now this is really absolute
        // because a :// follows the scheme
        result = NS_xstrdup(relpath);
      } else {
        // This is a deprecated form of relative urls like
        // http:file or http:/path/file
        // we will support it for now ...
        relative = true;
        offset = scheme.mLen + 1;
      }
    } else {
      // the schemes are not the same, we are also done
      // because we have to assume this is absolute
      result = NS_xstrdup(relpath);
    }
  } else {
    if (relpath[0] == '/' && relpath[1] == '/') {
      // this URL //host/path is almost absolute
      result = AppendToSubstring(mScheme.mPos, mScheme.mLen + 1, relpath);
    } else {
      // then it must be relative
      relative = true;
    }
  }
  if (relative) {
    uint32_t len = 0;
    const char* realrelpath = relpath + offset;
    switch (*realrelpath) {
      case '/':
        // overwrite everything after the authority
        len = mAuthority.mPos + mAuthority.mLen;
        break;
      case '?':
        // overwrite the existing ?query and #ref
        if (mQuery.mLen >= 0) {
          len = mQuery.mPos - 1;
        } else if (mRef.mLen >= 0) {
          len = mRef.mPos - 1;
        } else {
          len = mPath.mPos + mPath.mLen;
        }
        break;
      case '#':
      case '\0':
        // overwrite the existing #ref
        if (mRef.mLen < 0) {
          len = mPath.mPos + mPath.mLen;
        } else {
          len = mRef.mPos - 1;
        }
        break;
      default:
        if (protocol.IsEmpty() && Scheme() == "file" &&
            IsAsciiAlpha(realrelpath[0]) && realrelpath[1] == '|') {
          // For instance, <C|/foo/bar> against <file:///tmp/mock/path>
          // Treat tmp/mock/C|/foo/bar as /C|/foo/bar
          // + 1 should account for '/' at the beginning
          len = mAuthority.mPos + mAuthority.mLen + 1;
        } else {
          // overwrite everything after the directory
          len = mDirectory.mPos + mDirectory.mLen;
        }
    }
    result = AppendToSubstring(0, len, realrelpath);
    // locate result path
    resultPath = result + mPath.mPos;
  }
  if (!result) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (resultPath) {
    constexpr uint32_t slashDriveSpecifierLength = sizeof("/C:") - 1;
    // starting with file:C:/*
    // We need to ignore file:C: and begin from /
    // Note that file:C://* is already handled
    if (protocol.IsEmpty() && Scheme() == "file") {
      if (resultPath[0] == '/' && IsAsciiAlpha(resultPath[1]) &&
          (resultPath[2] == ':' || resultPath[2] == '|')) {
        resultPath += slashDriveSpecifierLength;
      }
    }

    // Edge case: <C|> against <file:///tmp/mock/path>
    if (resultPath && resultPath[0] == '/') {
      net_CoalesceDirs(resultPath);
    }
  } else {
    // locate result path
    resultPath = strstr(result, "://");
    if (resultPath) {
      // If there are multiple slashes after :// we must ignore them
      // otherwise net_CoalesceDirs may think the host is a part of the path.
      resultPath += 3;
      if (protocol.IsEmpty() && Scheme() != "file") {
        while (*resultPath == '/') {
          resultPath++;
        }
      }
      resultPath = strchr(resultPath, '/');
      if (resultPath) {
        net_CoalesceDirs(resultPath);
      }
    }
  }
  out.Adopt(result);
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetCommonBaseSpec(nsIURI* uri2, nsACString& aResult) {
  NS_ENSURE_ARG_POINTER(uri2);

  // if uri's are equal, then return uri as is
  bool isEquals = false;
  if (NS_SUCCEEDED(Equals(uri2, &isEquals)) && isEquals) {
    return GetSpec(aResult);
  }

  aResult.Truncate();

  // check pre-path; if they don't match, then return empty string
  RefPtr<nsStandardURL> stdurl2;
  nsresult rv = uri2->QueryInterface(kThisImplCID, getter_AddRefs(stdurl2));
  isEquals = NS_SUCCEEDED(rv) &&
             SegmentIs(mScheme, stdurl2->mSpec.get(), stdurl2->mScheme) &&
             SegmentIs(mHost, stdurl2->mSpec.get(), stdurl2->mHost) &&
             SegmentIs(mUsername, stdurl2->mSpec.get(), stdurl2->mUsername) &&
             SegmentIs(mPassword, stdurl2->mSpec.get(), stdurl2->mPassword) &&
             (Port() == stdurl2->Port());
  if (!isEquals) {
    return NS_OK;
  }

  // scan for first mismatched character
  const char *thisIndex, *thatIndex, *startCharPos;
  startCharPos = mSpec.get() + mDirectory.mPos;
  thisIndex = startCharPos;
  thatIndex = stdurl2->mSpec.get() + mDirectory.mPos;
  while ((*thisIndex == *thatIndex) && *thisIndex) {
    thisIndex++;
    thatIndex++;
  }

  // backup to just after previous slash so we grab an appropriate path
  // segment such as a directory (not partial segments)
  // todo:  also check for file matches which include '?' and '#'
  while ((thisIndex != startCharPos) && (*(thisIndex - 1) != '/')) {
    thisIndex--;
  }

  // grab spec from beginning to thisIndex
  aResult = Substring(mSpec, mScheme.mPos, thisIndex - mSpec.get());

  return rv;
}

NS_IMETHODIMP
nsStandardURL::GetRelativeSpec(nsIURI* uri2, nsACString& aResult) {
  NS_ENSURE_ARG_POINTER(uri2);

  aResult.Truncate();

  // if uri's are equal, then return empty string
  bool isEquals = false;
  if (NS_SUCCEEDED(Equals(uri2, &isEquals)) && isEquals) {
    return NS_OK;
  }

  RefPtr<nsStandardURL> stdurl2;
  nsresult rv = uri2->QueryInterface(kThisImplCID, getter_AddRefs(stdurl2));
  isEquals = NS_SUCCEEDED(rv) &&
             SegmentIs(mScheme, stdurl2->mSpec.get(), stdurl2->mScheme) &&
             SegmentIs(mHost, stdurl2->mSpec.get(), stdurl2->mHost) &&
             SegmentIs(mUsername, stdurl2->mSpec.get(), stdurl2->mUsername) &&
             SegmentIs(mPassword, stdurl2->mSpec.get(), stdurl2->mPassword) &&
             (Port() == stdurl2->Port());
  if (!isEquals) {
    return uri2->GetSpec(aResult);
  }

  // scan for first mismatched character
  const char *thisIndex, *thatIndex, *startCharPos;
  startCharPos = mSpec.get() + mDirectory.mPos;
  thisIndex = startCharPos;
  thatIndex = stdurl2->mSpec.get() + mDirectory.mPos;

#ifdef XP_WIN
  bool isFileScheme = SegmentIs(mScheme, "file");
  if (isFileScheme) {
    // on windows, we need to match the first segment of the path
    // if these don't match then we need to return an absolute path
    // skip over any leading '/' in path
    while ((*thisIndex == *thatIndex) && (*thisIndex == '/')) {
      thisIndex++;
      thatIndex++;
    }
    // look for end of first segment
    while ((*thisIndex == *thatIndex) && *thisIndex && (*thisIndex != '/')) {
      thisIndex++;
      thatIndex++;
    }

    // if we didn't match through the first segment, return absolute path
    if ((*thisIndex != '/') || (*thatIndex != '/')) {
      return uri2->GetSpec(aResult);
    }
  }
#endif

  while ((*thisIndex == *thatIndex) && *thisIndex) {
    thisIndex++;
    thatIndex++;
  }

  // backup to just after previous slash so we grab an appropriate path
  // segment such as a directory (not partial segments)
  // todo:  also check for file matches with '#' and '?'
  while ((*(thatIndex - 1) != '/') && (thatIndex != startCharPos)) {
    thatIndex--;
  }

  const char* limit = mSpec.get() + mFilepath.mPos + mFilepath.mLen;

  // need to account for slashes and add corresponding "../"
  for (; thisIndex <= limit && *thisIndex; ++thisIndex) {
    if (*thisIndex == '/') {
      aResult.AppendLiteral("../");
    }
  }

  // grab spec from thisIndex to end
  uint32_t startPos = stdurl2->mScheme.mPos + thatIndex - stdurl2->mSpec.get();
  aResult.Append(
      Substring(stdurl2->mSpec, startPos, stdurl2->mSpec.Length() - startPos));

  return rv;
}

//----------------------------------------------------------------------------
// nsStandardURL::nsIURL
//----------------------------------------------------------------------------

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetFilePath(nsACString& result) {
  result = Filepath();
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetQuery(nsACString& result) {
  result = Query();
  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::GetHasQuery(bool* result) {
  *result = (mQuery.mLen >= 0);
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetRef(nsACString& result) {
  result = Ref();
  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::GetHasRef(bool* result) {
  *result = (mRef.mLen >= 0);
  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::GetHasUserPass(bool* result) {
  *result = (mUsername.mLen >= 0) || (mPassword.mLen >= 0);
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetDirectory(nsACString& result) {
  result = Directory();
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetFileName(nsACString& result) {
  result = Filename();
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetFileBaseName(nsACString& result) {
  result = Basename();
  return NS_OK;
}

// result may contain unescaped UTF-8 characters
NS_IMETHODIMP
nsStandardURL::GetFileExtension(nsACString& result) {
  result = Extension();
  return NS_OK;
}

nsresult nsStandardURL::SetFilePath(const nsACString& input) {
  nsAutoCString str(input);
  str.StripTaggedASCII(ASCIIMask::MaskCRLFTab());
  const char* filepath = str.get();

  LOG(("nsStandardURL::SetFilePath [filepath=%s]\n", filepath));
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  // if there isn't a filepath, then there can't be anything
  // after the path either.  this url is likely uninitialized.
  if (mFilepath.mLen < 0) {
    return SetPathQueryRef(str);
  }

  if (!str.IsEmpty()) {
    nsAutoCString spec;
    uint32_t dirPos, basePos, extPos;
    int32_t dirLen, baseLen, extLen;
    nsresult rv;

    if (IsSpecialProtocol(mSpec)) {
      // Bug 1873955: Replace all backslashes with slashes when parsing paths
      // Stop when we reach the query or the hash.
      auto* start = str.BeginWriting();
      auto* end = str.EndWriting();
      while (start != end) {
        if (*start == '?' || *start == '#') {
          break;
        }
        if (*start == '\\') {
          *start = '/';
        }
        start++;
      }
    }

    rv = mParser->ParseFilePath(filepath, str.Length(), &dirPos, &dirLen,
                                &basePos, &baseLen, &extPos, &extLen);
    if (NS_FAILED(rv)) {
      return rv;
    }

    // build up new candidate spec
    spec.Assign(mSpec.get(), mPath.mPos);

    // ensure leading '/'
    if (filepath[dirPos] != '/') {
      spec.Append('/');
    }

    nsSegmentEncoder encoder;

    // append encoded filepath components
    if (dirLen > 0) {
      encoder.EncodeSegment(
          Substring(filepath + dirPos, filepath + dirPos + dirLen),
          esc_Directory | esc_AlwaysCopy, spec);
    }
    if (baseLen > 0) {
      encoder.EncodeSegment(
          Substring(filepath + basePos, filepath + basePos + baseLen),
          esc_FileBaseName | esc_AlwaysCopy, spec);
    }
    if (extLen >= 0) {
      spec.Append('.');
      if (extLen > 0) {
        encoder.EncodeSegment(
            Substring(filepath + extPos, filepath + extPos + extLen),
            esc_FileExtension | esc_AlwaysCopy, spec);
      }
    }

    // compute the ending position of the current filepath
    if (mFilepath.mLen >= 0) {
      uint32_t end = mFilepath.mPos + mFilepath.mLen;
      if (mSpec.Length() > end) {
        spec.Append(mSpec.get() + end, mSpec.Length() - end);
      }
    }

    return SetSpecInternal(spec);
  }
  if (mPath.mLen > 1) {
    mSpec.Cut(mPath.mPos + 1, mFilepath.mLen - 1);
    // left shift query, and ref
    ShiftFromQuery(1 - mFilepath.mLen);
    // One character for '/', and if we have a query or ref we add their
    // length and one extra for each '?' or '#' characters
    mPath.mLen = 1 + (mQuery.mLen >= 0 ? (mQuery.mLen + 1) : 0) +
                 (mRef.mLen >= 0 ? (mRef.mLen + 1) : 0);
    // these contain only a '/'
    mDirectory.mLen = 1;
    mFilepath.mLen = 1;
    // these are no longer defined
    mBasename.mLen = -1;
    mExtension.mLen = -1;
  }
  return NS_OK;
}

inline bool IsUTFEncoding(const Encoding* aEncoding) {
  return aEncoding == UTF_8_ENCODING || aEncoding == UTF_16BE_ENCODING ||
         aEncoding == UTF_16LE_ENCODING;
}

nsresult nsStandardURL::SetQuery(const nsACString& input) {
  return SetQueryWithEncoding(input, nullptr);
}

nsresult nsStandardURL::SetQueryWithEncoding(const nsACString& input,
                                             const Encoding* encoding) {
  const nsPromiseFlatCString& flat = PromiseFlatCString(input);
  const char* query = flat.get();

  LOG(("nsStandardURL::SetQuery [query=%s]\n", query));
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  if (IsUTFEncoding(encoding)) {
    encoding = nullptr;
  }

  if (mPath.mLen < 0) {
    return SetPathQueryRef(flat);
  }

  if (mSpec.Length() + input.Length() - Query().Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  InvalidateCache();

  if (flat.IsEmpty()) {
    // remove existing query
    if (mQuery.mLen >= 0) {
      // remove query and leading '?'
      mSpec.Cut(mQuery.mPos - 1, mQuery.mLen + 1);
      ShiftFromRef(-(mQuery.mLen + 1));
      mPath.mLen -= (mQuery.mLen + 1);
      mQuery.mPos = 0;
      mQuery.mLen = -1;
    }
    return NS_OK;
  }

  // filter out unexpected chars "\r\n\t" if necessary
  nsAutoCString filteredURI(flat);
  filteredURI.StripTaggedASCII(ASCIIMask::MaskCRLFTab());

  query = filteredURI.get();
  int32_t queryLen = filteredURI.Length();
  if (query[0] == '?') {
    query++;
    queryLen--;
  }

  // encode query if necessary
  nsAutoCString buf;
  bool encoded;
  nsSegmentEncoder encoder(encoding);
  encoder.EncodeSegmentCount(query, URLSegment(0, queryLen), esc_Query, buf,
                             encoded);
  if (encoded) {
    query = buf.get();
    queryLen = buf.Length();
  }

  // Check the final length after encoding, including any '?' to insert
  if (mQuery.mLen < 0 ? ExceedsMaxLength(0, queryLen + 1)
                      : ExceedsMaxLength(mQuery.mLen, queryLen)) {
    return NS_ERROR_MALFORMED_URI;
  }

  if (mQuery.mLen < 0) {
    if (mRef.mLen < 0) {
      mQuery.mPos = mSpec.Length();
    } else {
      mQuery.mPos = mRef.mPos - 1;
    }
    mSpec.Insert('?', mQuery.mPos);
    mQuery.mPos++;
    mQuery.mLen = 0;
    // the insertion pushes these out by 1
    mPath.mLen++;
    mRef.mPos++;
  }

  int32_t shift = ReplaceSegment(mQuery.mPos, mQuery.mLen, query, queryLen);

  if (shift) {
    mQuery.mLen = queryLen;
    mPath.mLen += shift;
    ShiftFromRef(shift);
  }
  return NS_OK;
}

nsresult nsStandardURL::SetRef(const nsACString& input) {
  const nsPromiseFlatCString& flat = PromiseFlatCString(input);
  const char* ref = flat.get();

  LOG(("nsStandardURL::SetRef [ref=%s]\n", ref));
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  if (mPath.mLen < 0) {
    return SetPathQueryRef(flat);
  }

  if (mSpec.Length() + input.Length() - Ref().Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  InvalidateCache();

  if (input.IsEmpty()) {
    // remove existing ref
    if (mRef.mLen >= 0) {
      // remove ref and leading '#'
      mSpec.Cut(mRef.mPos - 1, mRef.mLen + 1);
      mPath.mLen -= (mRef.mLen + 1);
      mRef.mPos = 0;
      mRef.mLen = -1;
    }
    return NS_OK;
  }

  // filter out unexpected chars "\r\n\t" if necessary
  nsAutoCString filteredURI(flat);
  filteredURI.StripTaggedASCII(ASCIIMask::MaskCRLFTab());

  ref = filteredURI.get();
  int32_t refLen = filteredURI.Length();
  if (ref[0] == '#') {
    ref++;
    refLen--;
  }

  // If percent encoding is necessary, `ref` will point to `buf`'s content.
  // `buf` needs to outlive any use of the `ref` pointer.
  nsAutoCString buf;
  // encode ref if necessary
  bool encoded;
  nsSegmentEncoder encoder;
  encoder.EncodeSegmentCount(ref, URLSegment(0, refLen), esc_Ref, buf, encoded);
  if (encoded) {
    ref = buf.get();
    refLen = buf.Length();
  }

  // Check the final length after encoding, including any '#' to append
  if (mRef.mLen < 0 ? ExceedsMaxLength(0, refLen + 1)
                    : ExceedsMaxLength(mRef.mLen, refLen)) {
    return NS_ERROR_MALFORMED_URI;
  }

  if (mRef.mLen < 0) {
    mSpec.Append('#');
    ++mPath.mLen;  // Include the # in the path.
    mRef.mPos = mSpec.Length();
    mRef.mLen = 0;
  }

  int32_t shift = ReplaceSegment(mRef.mPos, mRef.mLen, ref, refLen);
  mPath.mLen += shift;
  mRef.mLen = refLen;
  return NS_OK;
}

nsresult nsStandardURL::SetFileNameInternal(const nsACString& input) {
  const nsPromiseFlatCString& flat = PromiseFlatCString(input);
  const char* filename = flat.get();

  LOG(("nsStandardURL::SetFileNameInternal [filename=%s]\n", filename));
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });

  if (mPath.mLen < 0) {
    return SetPathQueryRef(flat);
  }

  if (mSpec.Length() + input.Length() - Filename().Length() >
      StaticPrefs::network_standard_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  int32_t shift = 0;

  if (!(filename && *filename)) {
    // remove the filename
    if (mBasename.mLen > 0) {
      if (mExtension.mLen >= 0) {
        mBasename.mLen += (mExtension.mLen + 1);
      }
      mSpec.Cut(mBasename.mPos, mBasename.mLen);
      shift = -mBasename.mLen;
      mBasename.mLen = 0;
      mExtension.mLen = -1;
    }
  } else {
    nsresult rv;
    uint32_t basenamePos = 0;
    int32_t basenameLen = -1;
    uint32_t extensionPos = 0;
    int32_t extensionLen = -1;
    // let the parser locate the basename and extension
    rv = mParser->ParseFileName(filename, flat.Length(), &basenamePos,
                                &basenameLen, &extensionPos, &extensionLen);
    if (NS_FAILED(rv)) {
      return rv;
    }

    URLSegment basename(basenamePos, basenameLen);
    URLSegment extension(extensionPos, extensionLen);

    if (basename.mLen < 0) {
      // remove existing filename
      if (mBasename.mLen >= 0) {
        uint32_t len = mBasename.mLen;
        if (mExtension.mLen >= 0) {
          len += (mExtension.mLen + 1);
        }
        mSpec.Cut(mBasename.mPos, len);
        shift = -int32_t(len);
        mBasename.mLen = 0;
        mExtension.mLen = -1;
      }
    } else {
      nsAutoCString newFilename;
      bool ignoredOut;
      nsSegmentEncoder encoder;
      basename.mLen = encoder.EncodeSegmentCount(
          filename, basename, esc_FileBaseName | esc_AlwaysCopy, newFilename,
          ignoredOut);
      if (extension.mLen >= 0) {
        newFilename.Append('.');
        extension.mLen = encoder.EncodeSegmentCount(
            filename, extension, esc_FileExtension | esc_AlwaysCopy,
            newFilename, ignoredOut);
      }

      if (mBasename.mLen < 0) {
        // insert new filename
        if (ExceedsMaxLength(0, newFilename.Length())) {
          return NS_ERROR_MALFORMED_URI;
        }
        mBasename.mPos = mDirectory.mPos + mDirectory.mLen;
        mSpec.Insert(newFilename, mBasename.mPos);
        shift = newFilename.Length();
      } else {
        // replace existing filename
        uint32_t oldLen = uint32_t(mBasename.mLen);
        if (mExtension.mLen >= 0) {
          oldLen += (mExtension.mLen + 1);
        }
        if (ExceedsMaxLength(oldLen, newFilename.Length())) {
          return NS_ERROR_MALFORMED_URI;
        }
        mSpec.Replace(mBasename.mPos, oldLen, newFilename);
        shift = newFilename.Length() - oldLen;
      }

      mBasename.mLen = basename.mLen;
      mExtension.mLen = extension.mLen;
      if (mExtension.mLen >= 0) {
        mExtension.mPos = mBasename.mPos + mBasename.mLen + 1;
      }
    }
  }
  if (shift) {
    ShiftFromQuery(shift);
    mFilepath.mLen += shift;
    mPath.mLen += shift;
  }
  return NS_OK;
}

nsresult nsStandardURL::SetFileBaseNameInternal(const nsACString& input) {
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });
  nsAutoCString extension;
  nsresult rv = GetFileExtension(extension);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString newFileName(input);

  if (!extension.IsEmpty()) {
    newFileName.Append('.');
    newFileName.Append(extension);
  }

  return SetFileNameInternal(newFileName);
}

nsresult nsStandardURL::SetFileExtensionInternal(const nsACString& input) {
  auto onExitGuard = MakeScopeExit([&] { SanityCheck(); });
  nsAutoCString newFileName;
  nsresult rv = GetFileBaseName(newFileName);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!input.IsEmpty()) {
    newFileName.Append('.');
    newFileName.Append(input);
  }

  return SetFileNameInternal(newFileName);
}

//----------------------------------------------------------------------------
// nsStandardURL::nsIFileURL
//----------------------------------------------------------------------------

nsresult nsStandardURL::ResolveFile(nsIFile** aResult) {
  MOZ_ASSERT(mSupportsFileURL,
             "ResolveFile() called on a URL that doesn't support files!");

  if (mFile) {
    return mFile->Clone(aResult);
  }

  // Parse the spec if we don't have a cached result
  if (mSpec.IsEmpty()) {
    NS_WARNING("url not initialized");
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!SegmentIs(mScheme, "file")) {
    NS_WARNING("not a file URL");
    return NS_ERROR_FAILURE;
  }

  return net_GetFileFromURLSpec(mSpec, aResult);
}

NS_IMETHODIMP
nsStandardURL::GetFile(nsIFile** result) {
  MOZ_ASSERT(mSupportsFileURL,
             "GetFile() called on a URL that doesn't support files!");

  nsCOMPtr<nsIFile> file;
  nsresult rv = ResolveFile(getter_AddRefs(file));
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (LOG_ENABLED()) {
    LOG(("nsStandardURL::GetFile [this=%p spec=%s resulting_path=%s]\n", this,
         mSpec.get(), file->HumanReadablePath().get()));
  }

  file.forget(result);
  return NS_OK;
}

nsresult nsStandardURL::SetFile(nsIFile* file) {
  NS_ENSURE_ARG_POINTER(file);

  nsresult rv;
  nsAutoCString url;

  rv = net_GetURLSpecFromFile(file, url);
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint32_t oldURLType = mURLType;
  uint32_t oldDefaultPort = mDefaultPort;
  rv = Init(nsIStandardURL::URLTYPE_NO_AUTHORITY, -1, url, nullptr, nullptr);

  if (NS_FAILED(rv)) {
    // Restore the old url type and default port if the call to Init fails.
    mURLType = oldURLType;
    mDefaultPort = oldDefaultPort;
    return rv;
  }

  // must clone |file| since its value is not guaranteed to remain constant
  InvalidateCache();
  if (NS_FAILED(file->Clone(getter_AddRefs(mFile)))) {
    NS_WARNING("nsIFile::Clone failed");
    // failure to clone is not fatal (GetFile will generate mFile)
    mFile = nullptr;
  }

  return NS_OK;
}

//----------------------------------------------------------------------------
// nsStandardURL::nsIStandardURL
//----------------------------------------------------------------------------

nsresult nsStandardURL::Init(uint32_t urlType, int32_t defaultPort,
                             const nsACString& spec, const char* charset,
                             nsIURI* baseURI) {
  if (spec.Length() > StaticPrefs::network_standard_url_max_length() ||
      defaultPort > std::numeric_limits<uint16_t>::max()) {
    return NS_ERROR_MALFORMED_URI;
  }

  InvalidateCache();

  switch (urlType) {
    case URLTYPE_STANDARD:
      mParser = net_GetStdURLParser();
      break;
    case URLTYPE_AUTHORITY:
      mParser = net_GetAuthURLParser();
      break;
    case URLTYPE_NO_AUTHORITY:
      mParser = net_GetNoAuthURLParser();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("bad urlType");
      return NS_ERROR_INVALID_ARG;
  }
  mDefaultPort = defaultPort;
  mURLType = urlType;

  const auto* encoding =
      charset ? Encoding::ForLabelNoReplacement(MakeStringSpan(charset))
              : nullptr;
  // URI can't be encoded in UTF-16BE or UTF-16LE. Truncate encoding
  // if it is one of utf encodings (since a null encoding implies
  // UTF-8, this is safe even if encoding is UTF-8).
  if (IsUTFEncoding(encoding)) {
    encoding = nullptr;
  }

  if (baseURI && net_IsAbsoluteURL(spec)) {
    baseURI = nullptr;
  }

  if (!baseURI) {
    return SetSpecWithEncoding(spec, encoding);
  }

  nsAutoCString buf;
  nsresult rv = baseURI->Resolve(spec, buf);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return SetSpecWithEncoding(buf, encoding);
}

nsresult nsStandardURL::SetDefaultPort(int32_t aNewDefaultPort) {
  InvalidateCache();

  // should never be more than 16 bit
  if (aNewDefaultPort >= std::numeric_limits<uint16_t>::max()) {
    return NS_ERROR_MALFORMED_URI;
  }

  // If we're already using the new default-port as a custom port, then clear
  // it off of our mSpec & set mPort to -1, to indicate that we'll be using
  // the default from now on (which happens to match what we already had).
  if (mPort == aNewDefaultPort) {
    ReplacePortInSpec(-1);
    mPort = -1;
  }
  mDefaultPort = aNewDefaultPort;

  return NS_OK;
}

//----------------------------------------------------------------------------
// nsStandardURL::nsISerializable
//----------------------------------------------------------------------------

NS_IMETHODIMP
nsStandardURL::Read(nsIObjectInputStream* stream) {
  MOZ_ASSERT_UNREACHABLE("Use nsIURIMutator.read() instead");
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsStandardURL::ReadPrivate(nsIObjectInputStream* stream) {
  MOZ_ASSERT(mDisplayHost.IsEmpty(), "Shouldn't have cached unicode host");

  // If we exit early, make sure to clear the URL so we don't fail the sanity
  // check in the destructor
  auto clearOnExit = MakeScopeExit([&] { Clear(); });

  nsresult rv;

  uint32_t urlType;
  rv = stream->Read32(&urlType);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mURLType = urlType;
  switch (mURLType) {
    case URLTYPE_STANDARD:
      mParser = net_GetStdURLParser();
      break;
    case URLTYPE_AUTHORITY:
      mParser = net_GetAuthURLParser();
      break;
    case URLTYPE_NO_AUTHORITY:
      mParser = net_GetNoAuthURLParser();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("bad urlType");
      return NS_ERROR_FAILURE;
  }

  rv = stream->Read32((uint32_t*)&mPort);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = stream->Read32((uint32_t*)&mDefaultPort);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = NS_ReadOptionalCString(stream, mSpec);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mScheme);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mAuthority);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mUsername);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mPassword);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mHost);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mPath);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mFilepath);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mDirectory);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mBasename);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mExtension);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // handle forward compatibility from older serializations that included mParam
  URLSegment old_param;
  rv = ReadSegment(stream, old_param);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mQuery);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ReadSegment(stream, mRef);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString oldOriginCharset;
  rv = NS_ReadOptionalCString(stream, oldOriginCharset);
  if (NS_FAILED(rv)) {
    return rv;
  }

  bool isMutable;
  rv = stream->ReadBoolean(&isMutable);
  if (NS_FAILED(rv)) {
    return rv;
  }
  (void)isMutable;

  bool supportsFileURL;
  rv = stream->ReadBoolean(&supportsFileURL);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mSupportsFileURL = supportsFileURL;

  if (!IsValid()) {
    return NS_ERROR_MALFORMED_URI;
  }

  // wait until object is set up, then modify path to include the param
  if (old_param.mLen >= 0) {  // note that mLen=0 is ";"
    // old_param is a local; IsValid() doesn't check it. Bounds-check
    // explicitly.
    CheckedInt<uint32_t> end = CheckedInt<uint32_t>(uint32_t(old_param.mPos)) +
                               uint32_t(old_param.mLen);
    if (!end.isValid() || end.value() > mSpec.Length()) {
      return NS_ERROR_MALFORMED_URI;
    }

    // If this wasn't empty, it marks characters between the end of the
    // file and start of the query - mPath should include the param,
    // query and ref already.  Bump the mFilePath and
    // directory/basename/extension components to include this.
    mFilepath.Merge(mSpec, ';', old_param);
    mDirectory.Merge(mSpec, ';', old_param);
    mBasename.Merge(mSpec, ';', old_param);
    mExtension.Merge(mSpec, ';', old_param);
  }

  NS_ENSURE_TRUE(mScheme.mPos == 0, NS_ERROR_MALFORMED_URI);
  NS_ENSURE_TRUE(mScheme.mLen > 0, NS_ERROR_MALFORMED_URI);
  // Make sure scheme is followed by :// (3 characters)
  NS_ENSURE_TRUE(mScheme.mLen < INT32_MAX - 3,
                 NS_ERROR_MALFORMED_URI);  // avoid overflow
  NS_ENSURE_TRUE(mSpec.Length() >= (uint32_t)mScheme.mLen + 3,
                 NS_ERROR_MALFORMED_URI);
  NS_ENSURE_TRUE(
      nsDependentCSubstring(mSpec, mScheme.mLen, 3).EqualsLiteral("://"),
      NS_ERROR_MALFORMED_URI);

  NS_ENSURE_TRUE(CheckSegmentInvariants(), NS_ERROR_MALFORMED_URI);

  if (StaticPrefs::network_ipc_reparse_deserialized_uri() &&
      XRE_IsParentProcess()) {
    nsAutoCString spec(mSpec);
    rv = SetSpecInternal(spec);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  rv = CheckIfHostIsAscii();
  if (NS_FAILED(rv)) {
    return rv;
  }

  clearOnExit.release();

  return NS_OK;
}

NS_IMETHODIMP
nsStandardURL::Write(nsIObjectOutputStream* stream) {
  MOZ_ASSERT(mSpec.Length() <= StaticPrefs::network_standard_url_max_length(),
             "The spec should never be this long, we missed a check.");
  nsresult rv;

  rv = stream->Write32(mURLType);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = stream->Write32(uint32_t(mPort));
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = stream->Write32(uint32_t(mDefaultPort));
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = NS_WriteOptionalStringZ(stream, mSpec.get());
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mScheme);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mAuthority);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mUsername);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mPassword);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mHost);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mPath);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mFilepath);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mDirectory);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mBasename);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mExtension);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // for backwards compatibility since we removed mParam.  Note that this will
  // mean that an older browser will read "" for mParam, and the param(s) will
  // be part of mPath (as they after the removal of special handling).  It only
  // matters if you downgrade a browser to before the patch.
  URLSegment empty;
  rv = WriteSegment(stream, empty);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mQuery);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WriteSegment(stream, mRef);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // former origin charset
  rv = NS_WriteOptionalStringZ(stream, "");
  if (NS_FAILED(rv)) {
    return rv;
  }

  // former mMutable
  rv = stream->WriteBoolean(false);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = stream->WriteBoolean(mSupportsFileURL);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // mDisplayHost is just a cache that can be recovered as needed.

  return NS_OK;
}

inline ipc::StandardURLSegment ToIPCSegment(
    const nsStandardURL::URLSegment& aSegment) {
  return ipc::StandardURLSegment(aSegment.mPos, aSegment.mLen);
}

[[nodiscard]] inline bool FromIPCSegment(
    const nsACString& aSpec, const ipc::StandardURLSegment& aSegment,
    nsStandardURL::URLSegment& aTarget) {
  // This seems to be just an empty segment.
  if (aSegment.length() == -1) {
    aTarget = nsStandardURL::URLSegment();
    return true;
  }

  // A value of -1 means an empty segment, but < -1 is undefined.
  if (NS_WARN_IF(aSegment.length() < -1)) {
    return false;
  }

  CheckedInt<uint32_t> segmentLen = aSegment.position();
  segmentLen += aSegment.length();
  // Make sure the segment does not extend beyond the spec.
  if (NS_WARN_IF(!segmentLen.isValid() ||
                 segmentLen.value() > aSpec.Length())) {
    return false;
  }

  aTarget.mPos = aSegment.position();
  aTarget.mLen = aSegment.length();

  return true;
}

void nsStandardURL::Serialize(URIParams& aParams) {
  MOZ_ASSERT(mSpec.Length() <= StaticPrefs::network_standard_url_max_length(),
             "The spec should never be this long, we missed a check.");
  StandardURLParams params;

  params.urlType() = mURLType;
  params.port() = mPort;
  params.defaultPort() = mDefaultPort;
  params.spec() = mSpec;
  params.scheme() = ToIPCSegment(mScheme);
  params.authority() = ToIPCSegment(mAuthority);
  params.username() = ToIPCSegment(mUsername);
  params.password() = ToIPCSegment(mPassword);
  params.host() = ToIPCSegment(mHost);
  params.path() = ToIPCSegment(mPath);
  params.filePath() = ToIPCSegment(mFilepath);
  params.directory() = ToIPCSegment(mDirectory);
  params.baseName() = ToIPCSegment(mBasename);
  params.extension() = ToIPCSegment(mExtension);
  params.query() = ToIPCSegment(mQuery);
  params.ref() = ToIPCSegment(mRef);
  params.supportsFileURL() = !!mSupportsFileURL;
  params.isSubstituting() = false;
  // mDisplayHost is just a cache that can be recovered as needed.

  aParams = params;
}

bool nsStandardURL::CheckSegmentInvariants() const {
  NS_ENSURE_TRUE(mPath.mLen != -1 && mSpec.CharAt(mPath.mPos) == '/', false);
  NS_ENSURE_TRUE(mPath.mPos == mFilepath.mPos, false);
  NS_ENSURE_TRUE(mQuery.mLen == -1 || (mQuery.mPos > mPath.mPos &&
                                       mSpec.CharAt(mQuery.mPos - 1) == '?'),
                 false);
  NS_ENSURE_TRUE(mRef.mLen == -1 || (mRef.mPos > mPath.mPos &&
                                     mSpec.CharAt(mRef.mPos - 1) == '#'),
                 false);
  NS_ENSURE_TRUE(mQuery.mLen == -1 || mRef.mLen == -1 ||
                     mQuery.mPos + mQuery.mLen < mRef.mPos,
                 false);

  // mDirectory, mBasename, mExtension must be sub-ranges of mFilepath,
  // which must be a sub-range of mPath.
  auto isSubSegment = [](const URLSegment& inner, const URLSegment& outer) {
    if (inner.mLen == -1) return true;
    if (outer.mLen == -1) return false;
    return inner.mPos >= outer.mPos &&
           inner.mPos + inner.mLen <= outer.mPos + outer.mLen;
  };
  NS_ENSURE_TRUE(isSubSegment(mFilepath, mPath), false);
  NS_ENSURE_TRUE(isSubSegment(mDirectory, mFilepath), false);
  NS_ENSURE_TRUE(isSubSegment(mBasename, mFilepath), false);
  NS_ENSURE_TRUE(isSubSegment(mExtension, mFilepath), false);
  NS_ENSURE_TRUE(isSubSegment(mHost, mAuthority), false);
  NS_ENSURE_TRUE(isSubSegment(mUsername, mAuthority), false);
  NS_ENSURE_TRUE(isSubSegment(mPassword, mAuthority), false);
  NS_ENSURE_TRUE(isSubSegment(mQuery, mPath), false);
  NS_ENSURE_TRUE(isSubSegment(mRef, mPath), false);

  // mPath must immediately follow mAuthority. If mAuthority is absent, that is
  // only valid for URLTYPE_NO_AUTHORITY (e.g. file: URLs parsed without an
  // authority component); for all other URL types a missing authority while
  // mPath is present indicates a malformed or crafted URL.
  if (mPath.mLen >= 0) {
    if (mAuthority.mLen >= 0) {
      NS_ENSURE_TRUE(mPath.mPos == mAuthority.mPos + mAuthority.mLen, false);
    } else {
      NS_ENSURE_TRUE(mURLType == URLTYPE_NO_AUTHORITY, false);
    }
  }

  NS_ENSURE_TRUE(mAuthority.mLen >= 0 || mPort == -1, false);
  return true;
}

bool nsStandardURL::Deserialize(const URIParams& aParams) {
  MOZ_ASSERT(mDisplayHost.IsEmpty(), "Shouldn't have cached unicode host");
  MOZ_ASSERT(!mFile, "Shouldn't have cached file");

  if (aParams.type() != URIParams::TStandardURLParams) {
    NS_ERROR("Received unknown parameters from the other process!");
    return false;
  }

  // If we exit early, make sure to clear the URL so we don't fail the sanity
  // check in the destructor
  auto clearOnExit = MakeScopeExit([&] { Clear(); });

  const StandardURLParams& params = aParams.get_StandardURLParams();

  mURLType = params.urlType();
  switch (mURLType) {
    case URLTYPE_STANDARD:
      mParser = net_GetStdURLParser();
      break;
    case URLTYPE_AUTHORITY:
      mParser = net_GetAuthURLParser();
      break;
    case URLTYPE_NO_AUTHORITY:
      mParser = net_GetNoAuthURLParser();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("bad urlType");
      return false;
  }

  mPort = params.port();
  mDefaultPort = params.defaultPort();
  mSpec = params.spec();
  ResetSpecHash();
  NS_ENSURE_TRUE(
      mSpec.Length() <= StaticPrefs::network_standard_url_max_length(), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.scheme(), mScheme), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.authority(), mAuthority), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.username(), mUsername), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.password(), mPassword), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.host(), mHost), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.path(), mPath), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.filePath(), mFilepath), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.directory(), mDirectory), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.baseName(), mBasename), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.extension(), mExtension), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.query(), mQuery), false);
  NS_ENSURE_TRUE(FromIPCSegment(mSpec, params.ref(), mRef), false);

  mSupportsFileURL = params.supportsFileURL();

  // Some sanity checks
  NS_ENSURE_TRUE(mScheme.mPos == 0, false);
  NS_ENSURE_TRUE(mScheme.mLen > 0, false);
  // Make sure scheme is followed by :// (3 characters)
  NS_ENSURE_TRUE(mScheme.mLen < INT32_MAX - 3, false);  // avoid overflow
  NS_ENSURE_TRUE(mSpec.Length() >= (uint32_t)mScheme.mLen + 3, false);
  NS_ENSURE_TRUE(
      nsDependentCSubstring(mSpec, mScheme.mLen, 3).EqualsLiteral("://"),
      false);
  NS_ENSURE_TRUE(CheckSegmentInvariants(), false);

  if (!IsValid()) {
    return false;
  }

  if (StaticPrefs::network_ipc_reparse_deserialized_uri() &&
      XRE_IsParentProcess()) {
    if (NS_FAILED(SetSpecInternal(params.spec()))) {
      return false;
    }
  }

  nsresult rv = CheckIfHostIsAscii();
  if (NS_FAILED(rv)) {
    return false;
  }

  clearOnExit.release();

  return true;
}

size_t nsStandardURL::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
  return aMallocSizeOf(this) +
         mSpec.SizeOfExcludingThisIfUnshared(aMallocSizeOf) +
         mDisplayHost.SizeOfExcludingThisIfUnshared(aMallocSizeOf);

  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - mParser
  // - mFile
}

}  // namespace net
}  // namespace mozilla
