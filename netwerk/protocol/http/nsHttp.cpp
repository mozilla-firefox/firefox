/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "nsHttp.h"

#include <errno.h>
#include <string.h>

#include <functional>

#include "CacheControlParser.h"
#include "HttpLog.h"
#include "PLDHashTable.h"
#include "mozilla/DataMutex.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_network.h"
#include "nsCRT.h"
#include "nsContentUtils.h"
#include "nsHttpHandler.h"
#include "nsHttpRequestHead.h"
#include "nsHttpResponseHead.h"
#include "nsICacheEntry.h"
#include "nsIRequest.h"
#include "nsIStandardURL.h"
#include "nsJSUtils.h"
#include "nsLiteralString.h"
#include "nsStandardURL.h"
#include "sslerr.h"

namespace mozilla {
namespace net {

extern const char kProxyType_SOCKS[];

// https://datatracker.ietf.org/doc/html/draft-ietf-webtrans-http3-15#section-4.4
constexpr uint64_t kWebTransportErrorCodeStart = 0x52e4a40fa8db;
// Covers the full unsigned long (0..2^32-1) streamErrorCode range: start +
// 0xffffffff + floor(0xffffffff / 0x1e), per draft-ietf-webtrans-http3
// section 4.4's error-code mapping.
constexpr uint64_t kWebTransportErrorCodeEnd = 0x52e5ac983162;

// find out how many atoms we have
#define HTTP_ATOM(_name, _value) Unused_##_name,
enum {
#include "nsHttpAtomList.inc"
  NUM_HTTP_ATOMS
};
#undef HTTP_ATOM

MOZ_RUNINIT static StaticDataMutex<
    nsTHashtable<nsCStringASCIICaseInsensitiveHashKey>>
    sAtomTable("nsHttp::sAtomTable");

// This is set to true in DestroyAtomTable so we don't try to repopulate the
// table if ResolveAtom gets called during shutdown for some reason.
static Atomic<bool> sTableDestroyed{false};

// We put the atoms in a hash table for speedy lookup.. see ResolveAtom.
namespace nsHttp {

nsresult CreateAtomTable(
    nsTHashtable<nsCStringASCIICaseInsensitiveHashKey>& base) {
  if (sTableDestroyed) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }
  // fill the table with our known atoms
  const nsHttpAtomLiteral* atoms[] = {
#define HTTP_ATOM(_name, _value) &(_name),
#include "nsHttpAtomList.inc"
#undef HTTP_ATOM
  };

  if (!base.IsEmpty()) {
    return NS_OK;
  }
  for (const auto* atom : atoms) {
    (void)base.PutEntry(atom->val(), fallible);
  }

  LOG(("Added static atoms to atomTable"));
  return NS_OK;
}

nsresult CreateAtomTable() {
  LOG(("CreateAtomTable"));
  auto atomTable = sAtomTable.Lock();
  return CreateAtomTable(atomTable.ref());
}

void DestroyAtomTable() {
  LOG(("DestroyAtomTable"));
  sTableDestroyed = true;
  auto atomTable = sAtomTable.Lock();
  atomTable.ref().Clear();
}

// this function may be called from multiple threads
nsHttpAtom ResolveAtom(const nsACString& str) {
  if (str.IsEmpty()) {
    return {};
  }

  auto atomTable = sAtomTable.Lock();

  if (atomTable.ref().IsEmpty()) {
    if (sTableDestroyed) {
      NS_WARNING("ResolveAtom called during shutdown");
      return {};
    }

    NS_WARNING("ResolveAtom called before CreateAtomTable");
    if (NS_FAILED(CreateAtomTable(atomTable.ref()))) {
      return {};
    }
  }

  // Check if we already have an entry in the table
  auto* entry = atomTable.ref().GetEntry(str);
  if (entry) {
    return nsHttpAtom(entry->GetKey());
  }

  LOG(("Putting %s header into atom table", nsPromiseFlatCString(str).get()));
  // Put the string in the table. If it works create the atom.
  entry = atomTable.ref().PutEntry(str, fallible);
  if (entry) {
    return nsHttpAtom(entry->GetKey());
  }
  return {};
}

//
// From section 2.2 of RFC 2616, a token is defined as:
//
//   token          = 1*<any CHAR except CTLs or separators>
//   CHAR           = <any US-ASCII character (octets 0 - 127)>
//   separators     = "(" | ")" | "<" | ">" | "@"
//                  | "," | ";" | ":" | "\" | <">
//                  | "/" | "[" | "]" | "?" | "="
//                  | "{" | "}" | SP | HT
//   CTL            = <any US-ASCII control character
//                    (octets 0 - 31) and DEL (127)>
//   SP             = <US-ASCII SP, space (32)>
//   HT             = <US-ASCII HT, horizontal-tab (9)>
//
static const char kValidTokenMap[128] = {
    0, 0, 0, 0, 0, 0, 0, 0,  //   0
    0, 0, 0, 0, 0, 0, 0, 0,  //   8
    0, 0, 0, 0, 0, 0, 0, 0,  //  16
    0, 0, 0, 0, 0, 0, 0, 0,  //  24

    0, 1, 0, 1, 1, 1, 1, 1,  //  32
    0, 0, 1, 1, 0, 1, 1, 0,  //  40
    1, 1, 1, 1, 1, 1, 1, 1,  //  48
    1, 1, 0, 0, 0, 0, 0, 0,  //  56

    0, 1, 1, 1, 1, 1, 1, 1,  //  64
    1, 1, 1, 1, 1, 1, 1, 1,  //  72
    1, 1, 1, 1, 1, 1, 1, 1,  //  80
    1, 1, 1, 0, 0, 0, 1, 1,  //  88

    1, 1, 1, 1, 1, 1, 1, 1,  //  96
    1, 1, 1, 1, 1, 1, 1, 1,  // 104
    1, 1, 1, 1, 1, 1, 1, 1,  // 112
    1, 1, 1, 0, 1, 0, 1, 0   // 120
};
bool IsValidToken(const char* start, const char* end) {
  if (start == end) return false;

  for (; start != end; ++start) {
    const unsigned char idx = *start;
    if (idx > 127 || !kValidTokenMap[idx]) return false;
  }

  return true;
}

const char* GetProtocolVersion(HttpVersion pv) {
  switch (pv) {
    case HttpVersion::v3_0:
      return "h3";
    case HttpVersion::v2_0:
      return "h2";
    case HttpVersion::v1_0:
      return "http/1.0";
    case HttpVersion::v1_1:
      return "http/1.1";
    default:
      NS_WARNING(nsPrintfCString("Unkown protocol version: 0x%X. "
                                 "Please file a bug",
                                 static_cast<uint32_t>(pv))
                     .get());
      return "http/1.1";
  }
}

// static
void TrimHTTPWhitespace(const nsACString& aSource, nsACString& aDest) {
  nsAutoCString str(aSource);

  // HTTP whitespace 0x09: '\t', 0x0A: '\n', 0x0D: '\r', 0x20: ' '
  static const char kHTTPWhitespace[] = "\t\n\r ";
  str.Trim(kHTTPWhitespace);
  aDest.Assign(str);
}

// static
bool IsReasonableHeaderValue(const nsACString& s) {
  // Header values MUST NOT contain line-breaks.  RFC 2616 technically
  // permits CTL characters, including CR and LF, in header values provided
  // they are quoted.  However, this can lead to problems if servers do not
  // interpret quoted strings properly.  Disallowing CR and LF here seems
  // reasonable and keeps things simple.  We also disallow a null byte.
  const nsACString::char_type* end = s.EndReading();
  for (const nsACString::char_type* i = s.BeginReading(); i != end; ++i) {
    if (*i == '\r' || *i == '\n' || *i == '\0') {
      return false;
    }
  }
  return true;
}

const char* FindToken(const char* input, const char* token, const char* seps) {
  if (!input) return nullptr;

  int inputLen = strlen(input);
  int tokenLen = strlen(token);

  if (inputLen < tokenLen) return nullptr;

  const char* inputTop = input;
  const char* inputEnd = input + inputLen - tokenLen;
  for (; input <= inputEnd; ++input) {
    if (nsCRT::strncasecmp(input, token, tokenLen) == 0) {
      if (input > inputTop && !strchr(seps, *(input - 1))) continue;
      if (input < inputEnd && !strchr(seps, *(input + tokenLen))) continue;
      return input;
    }
  }

  return nullptr;
}

bool ParseInt64(const char* input, const char** next, int64_t* r) {
  MOZ_ASSERT(input);
  MOZ_ASSERT(r);

  char* end = nullptr;
  errno = 0;  // Clear errno to make sure its value is set by strtoll
  int64_t value = strtoll(input, &end, /* base */ 10);

  // Fail if: - the parsed number overflows.
  //          - the end points to the start of the input string.
  //          - we parsed a negative value. Consumers don't expect that.
  if (errno != 0 || end == input || value < 0) {
    LOG(("nsHttp::ParseInt64 value=%" PRId64 " errno=%d", value, errno));
    return false;
  }

  if (next) {
    *next = end;
  }
  *r = value;
  return true;
}

bool IsPermanentRedirect(uint32_t httpStatus) {
  return httpStatus == 301 || httpStatus == 308;
}

bool ValidationRequired(bool isForcedValid,
                        nsHttpResponseHead* cachedResponseHead,
                        uint32_t loadFlags, bool allowStaleCacheContent,
                        bool forceValidateCacheContent, bool isImmutable,
                        bool customConditionalRequest,
                        nsHttpRequestHead& requestHead, nsICacheEntry* entry,
                        CacheControlParser& cacheControlRequest,
                        bool fromPreviousSession,
                        bool* performBackgroundRevalidation) {
  if (performBackgroundRevalidation) {
    *performBackgroundRevalidation = false;
  }

  // If the LOAD_FROM_CACHE flag is set, any cached data can simply be used
  if (loadFlags & nsIRequest::LOAD_FROM_CACHE || allowStaleCacheContent) {
    LOG(("NOT validating based on LOAD_FROM_CACHE load flag\n"));
    return false;
  }

  // If the VALIDATE_ALWAYS flag is set, any cached data won't be used until
  // it's revalidated with the server.
  if (((loadFlags & nsIRequest::VALIDATE_ALWAYS) ||
       forceValidateCacheContent) &&
      !isImmutable) {
    LOG(("Validating based on VALIDATE_ALWAYS load flag\n"));
    return true;
  }

  // Check isForcedValid to see if it is possible to skip validation.
  // Don't skip validation if we have serious reason to believe that this
  // content is invalid (it's expired).
  // See netwerk/cache2/nsICacheEntry.idl for details
  if (isForcedValid && (!cachedResponseHead->ExpiresInPast() ||
                        !cachedResponseHead->MustValidateIfExpired())) {
    LOG(("NOT validating based on isForcedValid being true.\n"));
    return false;
  }

  // Even if the VALIDATE_NEVER flag is set, there are still some cases in
  // which we must validate the cached response with the server.
  if (loadFlags & nsIRequest::VALIDATE_NEVER) {
    LOG(("VALIDATE_NEVER set\n"));
    // if no-store validate cached response (see bug 112564)
    if (cachedResponseHead->NoStore()) {
      LOG(("Validating based on no-store logic\n"));
      return true;
    }
    LOG(("NOT validating based on VALIDATE_NEVER load flag\n"));
    return false;
  }

  // check if validation is strictly required...
  if (cachedResponseHead->MustValidate()) {
    LOG(("Validating based on MustValidate() returning TRUE\n"));
    return true;
  }

  // possibly serve from cache for a custom If-Match/If-Unmodified-Since
  // conditional request
  if (customConditionalRequest && !requestHead.HasHeader(nsHttp::If_Match) &&
      !requestHead.HasHeader(nsHttp::If_Unmodified_Since)) {
    LOG(("Validating based on a custom conditional request\n"));
    return true;
  }

  // previously we also checked for a query-url w/out expiration
  // and didn't do heuristic on it. but defacto that is allowed now.
  //
  // Check if the cache entry has expired...

  bool doValidation = true;
  uint32_t now = NowInSeconds();

  uint32_t age = 0;
  nsresult rv = cachedResponseHead->ComputeCurrentAge(now, now, &age);
  if (NS_FAILED(rv)) {
    return true;
  }

  uint32_t freshness = 0;
  rv = cachedResponseHead->ComputeFreshnessLifetime(&freshness);
  if (NS_FAILED(rv)) {
    return true;
  }

  uint32_t expiration = 0;
  rv = entry->GetExpirationTime(&expiration);
  if (NS_FAILED(rv)) {
    return true;
  }

  uint32_t maxAgeRequest, maxStaleRequest, minFreshRequest;

  LOG(("  NowInSeconds()=%u, expiration time=%u, freshness lifetime=%u, age=%u",
       now, expiration, freshness, age));

  if (cacheControlRequest.NoCache()) {
    LOG(("  validating, no-cache request"));
    doValidation = true;
  } else if (cacheControlRequest.MaxStale(&maxStaleRequest)) {
    uint32_t staleTime = age > freshness ? age - freshness : 0;
    doValidation = staleTime > maxStaleRequest;
    LOG(("  validating=%d, max-stale=%u requested", doValidation,
         maxStaleRequest));
  } else if (cacheControlRequest.MaxAge(&maxAgeRequest)) {
    // The input information for age and freshness calculation are in seconds.
    // Hence, the internal logic can't have better resolution than seconds too.
    // To make max-age=0 case work even for requests made in less than a second
    // after the last response has been received, we use >= for compare.  This
    // is correct because of the rounding down of the age calculated value.
    doValidation = age >= maxAgeRequest;
    LOG(("  validating=%d, max-age=%u requested", doValidation, maxAgeRequest));
  } else if (cacheControlRequest.MinFresh(&minFreshRequest)) {
    uint32_t freshTime = freshness > age ? freshness - age : 0;
    doValidation = freshTime < minFreshRequest;
    LOG(("  validating=%d, min-fresh=%u requested", doValidation,
         minFreshRequest));
  } else if (now < expiration) {
    doValidation = false;
    LOG(("  not validating, expire time not in the past"));
  } else if (cachedResponseHead->MustValidateIfExpired()) {
    doValidation = true;
  } else if (cachedResponseHead->StaleWhileRevalidate(now, expiration) &&
             StaticPrefs::network_http_stale_while_revalidate_enabled()) {
    LOG(("  not validating, in the stall-while-revalidate window"));
    doValidation = false;
    if (performBackgroundRevalidation) {
      *performBackgroundRevalidation = true;
    }
  } else if (loadFlags & nsIRequest::VALIDATE_ONCE_PER_SESSION) {
    // If the cached response does not include expiration infor-
    // mation, then we must validate the response, despite whether
    // or not this is the first access this session.  This behavior
    // is consistent with existing browsers and is generally expected
    // by web authors.
    if (freshness == 0) {
      doValidation = true;
    } else {
      doValidation = fromPreviousSession;
    }
  } else {
    doValidation = true;
  }

  LOG(("%salidating based on expiration time\n", doValidation ? "V" : "Not v"));
  return doValidation;
}

nsresult GetHttpResponseHeadFromCacheEntry(
    nsICacheEntry* entry, nsHttpResponseHead* cachedResponseHead) {
  nsCString buf;
  // A "original-response-headers" metadata element holds network original
  // headers, i.e. the headers in the form as they arrieved from the network. We
  // need to get the network original headers first, because we need to keep
  // them in order.
  nsresult rv = entry->GetMetaDataElement("original-response-headers",
                                          getter_Copies(buf));
  if (NS_SUCCEEDED(rv)) {
    rv = cachedResponseHead->ParseCachedOriginalHeaders((char*)buf.get());
    if (NS_FAILED(rv)) {
      LOG(("  failed to parse original-response-headers\n"));
    }
  }

  buf.Adopt(nullptr);
  // A "response-head" metadata element holds response head, e.g. response
  // status line and headers in the form Firefox uses them internally (no
  // dupicate headers, etc.).
  rv = entry->GetMetaDataElement("response-head", getter_Copies(buf));
  NS_ENSURE_SUCCESS(rv, rv);

  // Parse string stored in a "response-head" metadata element.
  // These response headers will be merged with the orignal headers (i.e. the
  // headers stored in a "original-response-headers" metadata element).
  rv = cachedResponseHead->ParseCachedHead(buf.get());
  NS_ENSURE_SUCCESS(rv, rv);
  buf.Adopt(nullptr);

  return NS_OK;
}

nsresult CheckPartial(nsICacheEntry* aEntry, int64_t* aSize,
                      int64_t* aContentLength,
                      nsHttpResponseHead* responseHead) {
  nsresult rv;

  rv = aEntry->GetDataSize(aSize);

  if (NS_ERROR_IN_PROGRESS == rv) {
    *aSize = -1;
    rv = NS_OK;
  }

  NS_ENSURE_SUCCESS(rv, rv);

  if (!responseHead) {
    return NS_ERROR_UNEXPECTED;
  }

  *aContentLength = responseHead->ContentLength();

  return NS_OK;
}

void DetermineFramingAndImmutability(nsICacheEntry* entry,
                                     nsHttpResponseHead* responseHead,
                                     bool isHttps, bool* weaklyFramed,
                                     bool* isImmutable) {
  nsCString framedBuf;
  nsresult rv =
      entry->GetMetaDataElement("strongly-framed", getter_Copies(framedBuf));
  // describe this in terms of explicitly weakly framed so as to be backwards
  // compatible with old cache contents which dont have strongly-framed makers
  *weaklyFramed = NS_SUCCEEDED(rv) && framedBuf.EqualsLiteral("0");
  *isImmutable = !*weaklyFramed && isHttps && responseHead->Immutable();
}

bool IsBeforeLastActiveTabLoadOptimization(TimeStamp const& when) {
  return gHttpHandler &&
         gHttpHandler->IsBeforeLastActiveTabLoadOptimization(when);
}

nsCString ConvertRequestHeadToString(nsHttpRequestHead& aRequestHead,
                                     bool aHasRequestBody,
                                     bool aRequestBodyHasHeaders,
                                     bool aUsingConnect) {
  // Make sure that there is "Content-Length: 0" header in the requestHead
  // in case of POST and PUT methods when there is no requestBody and
  // requestHead doesn't contain "Transfer-Encoding" header.
  //
  // RFC1945 section 7.2.2:
  //   HTTP/1.0 requests containing an entity body must include a valid
  //   Content-Length header field.
  //
  // RFC2616 section 4.4:
  //   For compatibility with HTTP/1.0 applications, HTTP/1.1 requests
  //   containing a message-body MUST include a valid Content-Length header
  //   field unless the server is known to be HTTP/1.1 compliant.
  if ((aRequestHead.IsPost() || aRequestHead.IsPut()) && !aHasRequestBody &&
      !aRequestHead.HasHeader(nsHttp::Transfer_Encoding)) {
    DebugOnly<nsresult> rv =
        aRequestHead.SetHeader(nsHttp::Content_Length, "0"_ns);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  nsCString reqHeaderBuf;
  reqHeaderBuf.Truncate();

  // make sure we eliminate any proxy specific headers from
  // the request if we are using CONNECT
  aRequestHead.Flatten(reqHeaderBuf, aUsingConnect);

  if (!aRequestBodyHasHeaders || !aHasRequestBody) {
    reqHeaderBuf.AppendLiteral("\r\n");
  }

  return reqHeaderBuf;
}

void NotifyActiveTabLoadOptimization() {
  if (gHttpHandler) {
    gHttpHandler->NotifyActiveTabLoadOptimization();
  }
}

TimeStamp GetLastActiveTabLoadOptimizationHit() {
  return gHttpHandler ? gHttpHandler->GetLastActiveTabLoadOptimizationHit()
                      : TimeStamp();
}

void SetLastActiveTabLoadOptimizationHit(TimeStamp const& when) {
  if (gHttpHandler) {
    gHttpHandler->SetLastActiveTabLoadOptimizationHit(when);
  }
}

}  // namespace nsHttp

template <typename T>
void localEnsureBuffer(UniquePtr<T[]>& buf, uint32_t newSize, uint32_t preserve,
                       uint32_t& objSize) {
  if (objSize >= newSize) return;

  // Leave a little slop on the new allocation - add 2KB to
  // what we need and then round the result up to a 4KB (page)
  // boundary.

  objSize = (newSize + 2048 + 4095) & ~4095;

  static_assert(sizeof(T) == 1, "sizeof(T) must be 1");
  auto tmp = std::make_unique<T[]>(objSize);
  if (preserve) {
    memcpy(tmp.get(), buf.get(), preserve);
  }
  buf = std::move(tmp);
}

void EnsureBuffer(UniquePtr<char[]>& buf, uint32_t newSize, uint32_t preserve,
                  uint32_t& objSize) {
  localEnsureBuffer<char>(buf, newSize, preserve, objSize);
}

void EnsureBuffer(UniquePtr<uint8_t[]>& buf, uint32_t newSize,
                  uint32_t preserve, uint32_t& objSize) {
  localEnsureBuffer<uint8_t>(buf, newSize, preserve, objSize);
}

static bool IsTokenSymbol(signed char chr) {
  return !(chr < 33 || chr == 127 || chr == '(' || chr == ')' || chr == '<' ||
           chr == '>' || chr == '@' || chr == ',' || chr == ';' || chr == ':' ||
           chr == '"' || chr == '/' || chr == '[' || chr == ']' || chr == '?' ||
           chr == '=' || chr == '{' || chr == '}' || chr == '\\');
}

ParsedHeaderPair::ParsedHeaderPair(const char* name, int32_t nameLen,
                                   const char* val, int32_t valLen,
                                   bool isQuotedValue)
    : mName(nsDependentCSubstring(nullptr, size_t(0))),
      mValue(nsDependentCSubstring(nullptr, size_t(0))),
      mIsQuotedValue(isQuotedValue) {
  if (nameLen > 0) {
    mName.Rebind(name, name + nameLen);
  }
  if (valLen > 0) {
    if (mIsQuotedValue) {
      RemoveQuotedStringEscapes(val, valLen);
      mValue.Rebind(mUnquotedValue.BeginWriting(), mUnquotedValue.Length());
    } else {
      mValue.Rebind(val, val + valLen);
    }
  }
}

void ParsedHeaderPair::RemoveQuotedStringEscapes(const char* val,
                                                 int32_t valLen) {
  mUnquotedValue.Truncate();
  const char* c = val;
  for (int32_t i = 0; i < valLen; ++i) {
    if (c[i] == '\\' && c[i + 1]) {
      ++i;
    }
    mUnquotedValue.Append(c[i]);
  }
}

static void Tokenize(
    const char* input, uint32_t inputLen, const char token,
    const std::function<void(const char*, uint32_t)>& consumer) {
  auto trimWhitespace = [](const char* in, uint32_t inLen, const char** out,
                           uint32_t* outLen) {
    *out = in;
    *outLen = inLen;
    if (inLen == 0) {
      return;
    }

    // Trim leading space
    while (nsCRT::IsAsciiSpace(**out)) {
      (*out)++;
      --(*outLen);
    }

    // Trim tailing space
    for (const char* i = *out + *outLen - 1; i >= *out; --i) {
      if (!nsCRT::IsAsciiSpace(*i)) {
        break;
      }
      --(*outLen);
    }
  };

  const char* first = input;
  bool inQuote = false;
  const char* result = nullptr;
  uint32_t resultLen = 0;
  for (uint32_t index = 0; index < inputLen; ++index) {
    if (inQuote && input[index] == '\\' && input[index + 1]) {
      index++;
      continue;
    }
    if (input[index] == '"') {
      inQuote = !inQuote;
      continue;
    }
    if (inQuote) {
      continue;
    }
    if (input[index] == token) {
      trimWhitespace(first, (input + index) - first, &result, &resultLen);
      consumer(result, resultLen);
      first = input + index + 1;
    }
  }

  trimWhitespace(first, (input + inputLen) - first, &result, &resultLen);
  consumer(result, resultLen);
}

ParsedHeaderValueList::ParsedHeaderValueList(const char* t, uint32_t len,
                                             bool allowInvalidValue) {
  if (!len) {
    return;
  }

  ParsedHeaderValueList* self = this;
  auto consumer = [=](const char* output, uint32_t outputLength) {
    self->ParseNameAndValue(output, allowInvalidValue);
  };

  Tokenize(t, len, ';', consumer);
}

void ParsedHeaderValueList::ParseNameAndValue(const char* input,
                                              bool allowInvalidValue) {
  const char* nameStart = input;
  const char* nameEnd = nullptr;
  const char* valueStart = input;
  const char* valueEnd = nullptr;
  bool isQuotedString = false;
  bool invalidValue = false;

  for (; *input && *input != ';' && *input != ',' &&
         !nsCRT::IsAsciiSpace(*input) && *input != '=';
       input++) {
    ;
  }

  nameEnd = input;

  if (!(nameEnd - nameStart)) {
    return;
  }

  // Check whether param name is a valid token.
  for (const char* c = nameStart; c < nameEnd; c++) {
    if (!IsTokenSymbol(*c)) {
      nameEnd = c;
      break;
    }
  }

  if (!(nameEnd - nameStart)) {
    return;
  }

  while (nsCRT::IsAsciiSpace(*input)) {
    ++input;
  }

  if (!*input || *input++ != '=') {
    mValues.AppendElement(
        ParsedHeaderPair(nameStart, nameEnd - nameStart, nullptr, 0, false));
    return;
  }

  while (nsCRT::IsAsciiSpace(*input)) {
    ++input;
  }

  if (*input != '"') {
    // The value is a token, not a quoted string.
    valueStart = input;
    for (valueEnd = input; *valueEnd && !nsCRT::IsAsciiSpace(*valueEnd) &&
                           *valueEnd != ';' && *valueEnd != ',';
         valueEnd++) {
      ;
    }
    if (!allowInvalidValue) {
      for (const char* c = valueStart; c < valueEnd; c++) {
        if (!IsTokenSymbol(*c)) {
          valueEnd = c;
          break;
        }
      }
    }
  } else {
    bool foundQuotedEnd = false;
    isQuotedString = true;

    ++input;
    valueStart = input;
    for (valueEnd = input; *valueEnd; ++valueEnd) {
      if (*valueEnd == '\\' && *(valueEnd + 1)) {
        ++valueEnd;
      } else if (*valueEnd == '"') {
        foundQuotedEnd = true;
        break;
      }
    }
    if (!foundQuotedEnd) {
      invalidValue = true;
    }

    input = valueEnd;
    // *valueEnd != null means that *valueEnd is quote character.
    if (*valueEnd) {
      input++;
    }
  }

  if (invalidValue) {
    valueEnd = valueStart;
  }

  mValues.AppendElement(ParsedHeaderPair(nameStart, nameEnd - nameStart,
                                         valueStart, valueEnd - valueStart,
                                         isQuotedString));
}

ParsedHeaderValueListList::ParsedHeaderValueListList(
    const nsCString& fullHeader, bool allowInvalidValue)
    : mFull(fullHeader) {
  auto& values = mValues;
  auto consumer = [&values, allowInvalidValue](const char* output,
                                               uint32_t outputLength) {
    values.AppendElement(
        ParsedHeaderValueList(output, outputLength, allowInvalidValue));
  };

  Tokenize(mFull.BeginReading(), mFull.Length(), ',', consumer);
}

Maybe<nsCString> CallingScriptLocationString() {
  if (!LOG4_ENABLED() && !xpc::IsInAutomation()) {
    return Nothing();
  }

  JSContext* cx = nsContentUtils::GetCurrentJSContext();
  if (!cx) {
    return Nothing();
  }

  auto location = JSCallingLocation::Get(cx);
  nsCString logString = ""_ns;
  logString.AppendPrintf("%s:%u:%u", location.FileName().get(), location.mLine,
                         location.mColumn);
  return Some(logString);
}

void LogCallingScriptLocation(void* instance) {
  Maybe<nsCString> logLocation = CallingScriptLocationString();
  LogCallingScriptLocation(instance, logLocation);
}

void LogCallingScriptLocation(void* instance,
                              const Maybe<nsCString>& aLogLocation) {
  if (aLogLocation.isNothing()) {
    return;
  }

  nsCString logString;
  logString.AppendPrintf("%p called from script: ", instance);
  logString.AppendPrintf("%s", aLogLocation->get());
  LOG(("%s", logString.get()));
}

void LogHeaders(const char* lineStart) {
  nsAutoCString buf;
  const char* endOfLine;
  while ((endOfLine = strstr(lineStart, "\r\n"))) {
    buf.Assign(lineStart, endOfLine - lineStart);
    if (StaticPrefs::network_http_sanitize_headers_in_logs() &&
        (nsCRT::strcasestr(buf.get(), "authorization: ") ||
         nsCRT::strcasestr(buf.get(), "proxy-authorization: "))) {
      char* p = strchr(buf.BeginWriting(), ' ');
      while (p && *++p) {
        *p = '*';
      }
    }
    LOG1(("  %s\n", buf.get()));
    lineStart = endOfLine + 2;
  }
}

nsresult HttpProxyResponseToErrorCode(uint32_t aStatusCode) {
  // In proxy CONNECT case, we treat every response code except 200 as an error.
  // Even if the proxy server returns other 2xx codes (i.e. 206), this function
  // still returns an error code.
  MOZ_ASSERT(aStatusCode != 200);

  nsresult rv;
  switch (aStatusCode) {
    case 300:
    case 301:
    case 302:
    case 303:
    case 307:
    case 308:
      // Bad redirect: not top-level, or it's a POST, bad/missing Location,
      // or ProcessRedirect() failed for some other reason.  Legal
      // redirects that fail because site not available, etc., are handled
      // elsewhere, in the regular codepath.
      rv = NS_ERROR_CONNECTION_REFUSED;
      break;
    // Squid sends 404 if DNS fails (regular 404 from target is tunneled)
    case 404:  // HTTP/1.1: "Not Found"
               // RFC 2616: "some deployed proxies are known to return 400 or
               // 500 when DNS lookups time out."  (Squid uses 500 if it runs
               // out of sockets: so we have a conflict here).
    case 400:  // HTTP/1.1 "Bad Request"
    case 500:  // HTTP/1.1: "Internal Server Error"
      rv = NS_ERROR_UNKNOWN_HOST;
      break;
    case 401:
      rv = NS_ERROR_PROXY_UNAUTHORIZED;
      break;
    case 402:
      rv = NS_ERROR_PROXY_PAYMENT_REQUIRED;
      break;
    case 403:
      rv = NS_ERROR_PROXY_FORBIDDEN;
      break;
    case 405:
      rv = NS_ERROR_PROXY_METHOD_NOT_ALLOWED;
      break;
    case 406:
      rv = NS_ERROR_PROXY_NOT_ACCEPTABLE;
      break;
    case 407:  // ProcessAuthentication() failed (e.g. no header)
      rv = NS_ERROR_PROXY_AUTHENTICATION_FAILED;
      break;
    case 408:
      rv = NS_ERROR_PROXY_REQUEST_TIMEOUT;
      break;
    case 409:
      rv = NS_ERROR_PROXY_CONFLICT;
      break;
    case 410:
      rv = NS_ERROR_PROXY_GONE;
      break;
    case 411:
      rv = NS_ERROR_PROXY_LENGTH_REQUIRED;
      break;
    case 412:
      rv = NS_ERROR_PROXY_PRECONDITION_FAILED;
      break;
    case 413:
      rv = NS_ERROR_PROXY_REQUEST_ENTITY_TOO_LARGE;
      break;
    case 414:
      rv = NS_ERROR_PROXY_REQUEST_URI_TOO_LONG;
      break;
    case 415:
      rv = NS_ERROR_PROXY_UNSUPPORTED_MEDIA_TYPE;
      break;
    case 416:
      rv = NS_ERROR_PROXY_REQUESTED_RANGE_NOT_SATISFIABLE;
      break;
    case 417:
      rv = NS_ERROR_PROXY_EXPECTATION_FAILED;
      break;
    case 421:
      rv = NS_ERROR_PROXY_MISDIRECTED_REQUEST;
      break;
    case 425:
      rv = NS_ERROR_PROXY_TOO_EARLY;
      break;
    case 426:
      rv = NS_ERROR_PROXY_UPGRADE_REQUIRED;
      break;
    case 428:
      rv = NS_ERROR_PROXY_PRECONDITION_REQUIRED;
      break;
    case 429:
      rv = NS_ERROR_PROXY_TOO_MANY_REQUESTS;
      break;
    case 431:
      rv = NS_ERROR_PROXY_REQUEST_HEADER_FIELDS_TOO_LARGE;
      break;
    case 451:
      rv = NS_ERROR_PROXY_UNAVAILABLE_FOR_LEGAL_REASONS;
      break;
    case 501:
      rv = NS_ERROR_PROXY_NOT_IMPLEMENTED;
      break;
    case 502:
      rv = NS_ERROR_PROXY_BAD_GATEWAY;
      break;
    case 503:
      // Squid returns 503 if target request fails for anything but DNS.
      /* User sees: "Failed to Connect:
       *  Firefox can't establish a connection to the server at
       *  www.foo.com.  Though the site seems valid, the browser
       *  was unable to establish a connection."
       */
      rv = NS_ERROR_CONNECTION_REFUSED;
      break;
    // RFC 2616 uses 504 for both DNS and target timeout, so not clear what to
    // do here: picking target timeout, as DNS covered by 400/404/500
    case 504:
      rv = NS_ERROR_PROXY_GATEWAY_TIMEOUT;
      break;
    case 505:
      rv = NS_ERROR_PROXY_VERSION_NOT_SUPPORTED;
      break;
    case 506:
      rv = NS_ERROR_PROXY_VARIANT_ALSO_NEGOTIATES;
      break;
    case 510:
      rv = NS_ERROR_PROXY_NOT_EXTENDED;
      break;
    case 511:
      rv = NS_ERROR_PROXY_NETWORK_AUTHENTICATION_REQUIRED;
      break;
    default:
      rv = NS_ERROR_PROXY_CONNECTION_REFUSED;
      break;
  }

  return rv;
}

SupportedAlpnRank IsAlpnSupported(const nsACString& aAlpn) {
  if (nsHttpHandler::IsHttp3Enabled() &&
      gHttpHandler->IsHttp3VersionSupported(aAlpn)) {
    return SupportedAlpnRank::HTTP_3_VER_1;
  }

  if (StaticPrefs::network_http_http2_enabled()) {
    SpdyInformation* spdyInfo = gHttpHandler->SpdyInfo();
    if (aAlpn.Equals(spdyInfo->VersionString)) {
      return SupportedAlpnRank::HTTP_2;
    }
  }

  if (aAlpn.LowerCaseEqualsASCII("http/1.1")) {
    return SupportedAlpnRank::HTTP_1_1;
  }

  return SupportedAlpnRank::NOT_SUPPORTED;
}

// NSS errors that may be triggered by early data or PSK session resumption
// (e.g. badly behaving middleboxes, or a server whose session ticket
// encryption key has rotated). We re-attempt the connection without early
// data; MaybeRemoveSSLToken evicts the stale token so it is not re-offered.
bool PossibleZeroRTTRetryError(nsresult aReason) {
  return (aReason ==
          psm::GetXPCOMFromNSSError(SSL_ERROR_PROTOCOL_VERSION_ALERT)) ||
         (aReason == psm::GetXPCOMFromNSSError(SSL_ERROR_BAD_MAC_ALERT)) ||
         (aReason ==
          psm::GetXPCOMFromNSSError(SSL_ERROR_HANDSHAKE_UNEXPECTED_ALERT)) ||
         (aReason == psm::GetXPCOMFromNSSError(SSL_ERROR_DECRYPT_ERROR_ALERT));
}

nsresult MakeOriginURL(const nsACString& origin, nsCOMPtr<nsIURI>& url) {
  nsAutoCString scheme;
  nsresult rv = net_ExtractURLScheme(origin, scheme);
  NS_ENSURE_SUCCESS(rv, rv);
  return MakeOriginURL(scheme, origin, url);
}

nsresult MakeOriginURL(const nsACString& scheme, const nsACString& origin,
                       nsCOMPtr<nsIURI>& url) {
  return NS_MutateURI(new nsStandardURL::Mutator())
      .Apply(&nsIStandardURLMutator::Init, nsIStandardURL::URLTYPE_AUTHORITY,
             scheme.EqualsLiteral("http") ? NS_HTTP_DEFAULT_PORT
                                          : NS_HTTPS_DEFAULT_PORT,
             origin, nullptr, nullptr, nullptr)
      .Finalize(url);
}

void CreatePushHashKey(const nsCString& scheme, const nsCString& hostHeader,
                       const mozilla::OriginAttributes& originAttributes,
                       uint64_t serial, const nsACString& pathInfo,
                       nsCString& outOrigin, nsCString& outKey) {
  nsCString fullOrigin = scheme;
  fullOrigin.AppendLiteral("://");
  fullOrigin.Append(hostHeader);

  nsCOMPtr<nsIURI> origin;
  nsresult rv = MakeOriginURL(scheme, fullOrigin, origin);

  if (NS_SUCCEEDED(rv)) {
    rv = origin->GetAsciiSpec(outOrigin);
    outOrigin.Trim("/", false, true, false);
  }

  if (NS_FAILED(rv)) {
    // Fallback to plain text copy - this may end up behaving poorly
    outOrigin = std::move(fullOrigin);
  }

  outKey = outOrigin;
  outKey.AppendLiteral("/[");
  nsAutoCString suffix;
  originAttributes.CreateSuffix(suffix);
  outKey.Append(suffix);
  outKey.Append(']');
  outKey.AppendLiteral("/[http2.");
  outKey.AppendInt(serial);
  outKey.Append(']');
  outKey.Append(pathInfo);
}

nsresult GetNSResultFromWebTransportError(uint8_t aErrorCode) {
  return static_cast<nsresult>((uint32_t)NS_ERROR_WEBTRANSPORT_CODE_BASE +
                               (uint32_t)aErrorCode);
}

uint8_t GetWebTransportErrorFromNSResult(nsresult aResult) {
  if (aResult < NS_ERROR_WEBTRANSPORT_CODE_BASE ||
      aResult > NS_ERROR_WEBTRANSPORT_CODE_END) {
    return 0;
  }

  return static_cast<uint8_t>((uint32_t)aResult -
                              (uint32_t)NS_ERROR_WEBTRANSPORT_CODE_BASE);
}

uint64_t WebTransportErrorToHttp3Error(uint32_t aErrorCode) {
  return kWebTransportErrorCodeStart + aErrorCode +
         static_cast<uint64_t>(aErrorCode) / 0x1e;
}

Maybe<uint32_t> Http3ErrorToWebTransportError(uint64_t aErrorCode) {
  // Outside the range reserved for WT_APPLICATION_ERROR, or one of the
  // reserved (GREASE) codepoints inside it: not an application error code.
  if (aErrorCode < kWebTransportErrorCodeStart ||
      aErrorCode > kWebTransportErrorCodeEnd ||
      (aErrorCode - 0x21) % 0x1f == 0) {
    return Nothing();
  }

  uint64_t shifted = aErrorCode - kWebTransportErrorCodeStart;
  return Some(static_cast<uint32_t>(shifted - shifted / 0x1f));
}

ConnectionCloseReason ToCloseReason(nsresult aErrorCode) {
  if (NS_SUCCEEDED(aErrorCode)) {
    return ConnectionCloseReason::OK;
  }

  if (aErrorCode == NS_ERROR_UNKNOWN_HOST) {
    return ConnectionCloseReason::DNS_ERROR;
  }
  if (aErrorCode == NS_ERROR_NET_RESET) {
    return ConnectionCloseReason::NET_RESET;
  }
  if (aErrorCode == NS_ERROR_CONNECTION_REFUSED) {
    return ConnectionCloseReason::NET_REFUSED;
  }
  if (aErrorCode == NS_ERROR_SOCKET_ADDRESS_NOT_SUPPORTED) {
    return ConnectionCloseReason::SOCKET_ADDRESS_NOT_SUPPORTED;
  }
  if (aErrorCode == NS_ERROR_NET_TIMEOUT) {
    return ConnectionCloseReason::NET_TIMEOUT;
  }
  if (aErrorCode == NS_ERROR_OUT_OF_MEMORY) {
    return ConnectionCloseReason::OUT_OF_MEMORY;
  }
  if (aErrorCode == NS_ERROR_SOCKET_ADDRESS_IN_USE) {
    return ConnectionCloseReason::SOCKET_ADDRESS_IN_USE;
  }
  if (aErrorCode == NS_BINDING_ABORTED) {
    return ConnectionCloseReason::BINDING_ABORTED;
  }
  if (aErrorCode == NS_BINDING_REDIRECTED) {
    return ConnectionCloseReason::BINDING_REDIRECTED;
  }
  if (aErrorCode == NS_ERROR_ABORT) {
    return ConnectionCloseReason::ERROR_ABORT;
  }

  int32_t code = -1 * NS_ERROR_GET_CODE(aErrorCode);
  if (mozilla::psm::IsNSSErrorCode(code)) {
    return ConnectionCloseReason::SECURITY_ERROR;
  }

  return ConnectionCloseReason::OTHER_NET_ERROR;
}

void DisallowHTTPSRR(uint32_t& aCaps) {
  // NS_HTTP_DISALLOW_HTTPS_RR should take precedence than
  // NS_HTTP_FORCE_WAIT_HTTP_RR.
  aCaps = (aCaps | NS_HTTP_DISALLOW_HTTPS_RR) & ~NS_HTTP_FORCE_WAIT_HTTP_RR;
}

// Convert HttpVersion enum to Telemetry label string
nsLiteralCString HttpVersionToTelemetryLabel(HttpVersion version) {
  switch (version) {
    case HttpVersion::v0_9:
    case HttpVersion::v1_0:
    case HttpVersion::v1_1:
      return "http_1"_ns;
    case HttpVersion::v2_0:
      return "http_2"_ns;
    case HttpVersion::v3_0:
      return "http_3"_ns;
    default:
      break;
  }
  return "unknown"_ns;
}

nsIHttpChannelInternal::ProxyDNSStrategy GetProxyDNSStrategyHelper(
    const char* aType, uint32_t aFlag) {
  if (!aType) {
    return nsIHttpChannelInternal::PROXY_DNS_STRATEGY_ORIGIN;
  }

  if (!(aFlag & nsIProxyInfo::TRANSPARENT_PROXY_RESOLVES_HOST)) {
    if (aType == kProxyType_SOCKS) {
      return nsIHttpChannelInternal::PROXY_DNS_STRATEGY_ORIGIN;
    }
  }

  return nsIHttpChannelInternal::PROXY_DNS_STRATEGY_PROXY;
}

nsIHttpChannelInternal::ProxyType GetProxyTypeHelper(nsIProxyInfo* aProxyInfo) {
  if (!aProxyInfo) {
    return nsIHttpChannelInternal::PROXY_TYPE_DIRECT;
  }

  nsAutoCString type;
  uint32_t flags = 0;
  aProxyInfo->GetType(type);
  aProxyInfo->GetFlags(&flags);
  bool proxyResolvesHost =
      flags & nsIProxyInfo::TRANSPARENT_PROXY_RESOLVES_HOST;
  if (type.EqualsLiteral("direct")) {
    return nsIHttpChannelInternal::PROXY_TYPE_DIRECT;
  }
  if (type.EqualsLiteral("http")) {
    return nsIHttpChannelInternal::PROXY_TYPE_HTTP;
  }
  if (type.EqualsLiteral("https")) {
    return nsIHttpChannelInternal::PROXY_TYPE_HTTPS;
  }
  if (type.EqualsLiteral("socks4")) {
    return proxyResolvesHost ? nsIHttpChannelInternal::PROXY_TYPE_SOCKS4A
                             : nsIHttpChannelInternal::PROXY_TYPE_SOCKS4;
  }
  if (type.EqualsLiteral("socks") || type.EqualsLiteral("socks5")) {
    return proxyResolvesHost ? nsIHttpChannelInternal::PROXY_TYPE_SOCKS5H
                             : nsIHttpChannelInternal::PROXY_TYPE_SOCKS5;
  }
  if (type.EqualsLiteral("masque")) {
    return nsIHttpChannelInternal::PROXY_TYPE_MASQUE;
  }
  return nsIHttpChannelInternal::PROXY_TYPE_UNKNOWN;
}

}  // namespace net
}  // namespace mozilla
