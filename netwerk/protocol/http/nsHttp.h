/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttp_h_
#define nsHttp_h_

#include <stdint.h>

#include "NSSErrorsService.h"
#include "mozilla/Maybe.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "nsError.h"
#include "nsIHttpChannelInternal.h"
#include "nsString.h"
#include "nsTArray.h"
#include "prtime.h"

class nsICacheEntry;

namespace mozilla {

namespace net {
class nsHttpResponseHead;
class nsHttpRequestHead;
class CacheControlParser;

enum class HttpVersion {
  UNKNOWN = 0,
  v0_9 = 9,
  v1_0 = 10,
  v1_1 = 11,
  v2_0 = 20,
  v3_0 = 30
};

enum class SpdyVersion { NONE = 0, HTTP_2 = 5 };

enum class SupportedAlpnRank : uint8_t {
  NOT_SUPPORTED = 0,
  HTTP_1_1 = 1,
  HTTP_2 = 2,
  HTTP_3_VER_1 = 3,
};

// IMPORTANT: when adding new values, always add them to the end, otherwise
// it will mess up telemetry.
enum class ConnectionCloseReason : uint32_t {
  UNSET = 0,
  OK,
  IDLE_TIMEOUT,
  TLS_TIMEOUT,
  GO_AWAY,
  DNS_ERROR,
  NET_RESET,
  NET_TIMEOUT,
  NET_REFUSED,
  NET_INTERRUPT,
  NET_INADEQ_SEQURITY,
  SOCKET_ADDRESS_NOT_SUPPORTED,
  OUT_OF_MEMORY,
  SOCKET_ADDRESS_IN_USE,
  BINDING_ABORTED,
  BINDING_REDIRECTED,
  ERROR_ABORT,
  CLOSE_EXISTING_CONN_FOR_COALESCING,
  CLOSE_NEW_CONN_FOR_COALESCING,
  CANT_REUSED,
  OTHER_NET_ERROR,
  SECURITY_ERROR,
};

ConnectionCloseReason ToCloseReason(nsresult aErrorCode);

inline bool IsHttp3(SupportedAlpnRank aRank) {
  return aRank == SupportedAlpnRank::HTTP_3_VER_1;
}

//-----------------------------------------------------------------------------
// http connection capabilities
//-----------------------------------------------------------------------------

#define NS_HTTP_ALLOW_KEEPALIVE (1 << 0)
#define NS_HTTP_LARGE_KEEPALIVE (1 << 1)

// a transaction with this caps flag will continue to own the connection,
// preventing it from being reclaimed, even after the transaction completes.
#define NS_HTTP_STICKY_CONNECTION (1 << 2)

// a transaction with this caps flag will, upon opening a new connection,
// bypass the local DNS cache
#define NS_HTTP_REFRESH_DNS (1 << 3)

// a transaction with this caps flag will not pass SSL client-certificates
// to the server (see bug #466080), but is may also be used for other things
#define NS_HTTP_LOAD_ANONYMOUS (1 << 4)

// a transaction with this flag blocks the initiation of other transactons
// in the same load group until it is complete
#define NS_HTTP_LOAD_AS_BLOCKING (1 << 6)

// Disallow the use of the SPDY protocol. This is meant for the contexts
// such as HTTP upgrade which are nonsensical for SPDY, it is not the
// SPDY configuration variable.
#define NS_HTTP_DISALLOW_SPDY (1 << 7)

// a transaction with this flag loads without respect to whether the load
// group is currently blocking on some resources
#define NS_HTTP_LOAD_UNBLOCKED (1 << 8)

// This flag indicates the transaction should accept associated pushes
#define NS_HTTP_ONPUSH_LISTENER (1 << 9)

// Transactions with this flag should react to errors without side effects
// First user is to prevent clearing of alt-svc cache on failed probe
#define NS_HTTP_ERROR_SOFTLY (1 << 10)

// This corresponds to nsIHttpChannelInternal.beConservative
// it disables any cutting edge features that we are worried might result in
// interop problems with critical infrastructure
#define NS_HTTP_BE_CONSERVATIVE (1 << 11)

// Transactions with this flag should be processed first.
#define NS_HTTP_URGENT_START (1 << 12)

// A sticky connection of the transaction is explicitly allowed to be restarted
// on ERROR_NET_RESET.
#define NS_HTTP_CONNECTION_RESTARTABLE (1 << 13)

// Allow re-using a spdy/http2 connection with NS_HTTP_ALLOW_KEEPALIVE not set.
// This is primarily used to allow connection sharing for websockets over http/2
// without accidentally allowing it for websockets not over http/2
#define NS_HTTP_ALLOW_SPDY_WITHOUT_KEEPALIVE (1 << 15)

// Only permit CONNECTing to a proxy. A channel with this flag will not send an
// http request after CONNECT or setup tls. An http upgrade handler MUST be
// set. An ALPN header is set using the upgrade protocol.
#define NS_HTTP_CONNECT_ONLY (1 << 16)

// The connection should not use IPv4.
#define NS_HTTP_DISABLE_IPV4 (1 << 17)

// The connection should not use IPv6
#define NS_HTTP_DISABLE_IPV6 (1 << 18)

// Encodes the TRR mode.
#define NS_HTTP_TRR_MODE_MASK ((1 << 19) | (1 << 20))

// Disallow the use of the HTTP3 protocol. This is meant for the contexts
// such as HTTP upgrade which are not supported by HTTP3.
#define NS_HTTP_DISALLOW_HTTP3 (1 << 21)

// Force a transaction to stay in pending queue until the HTTPS RR is
// available.
#define NS_HTTP_FORCE_WAIT_HTTP_RR (1 << 22)

// This is used for a temporary workaround for a web-compat issue. The flag is
// only set on CORS preflight request to allowed sending client certificates
// on a connection for an anonymous request.
#define NS_HTTP_LOAD_ANONYMOUS_CONNECT_ALLOW_CLIENT_CERT (1 << 23)

#define NS_HTTP_DISALLOW_HTTPS_RR (1 << 24)

#define NS_HTTP_DISALLOW_ECH (1 << 25)

// Used to indicate that an HTTP Connection should obey Resist Fingerprinting
// and set the User-Agent accordingly.
#define NS_HTTP_USE_RFP (1 << 26)

// If set, then the initial TLS handshake failed.
#define NS_HTTP_IS_RETRY (1 << 27)

// When set, disallow to connect to a HTTP/2 proxy.
#define NS_HTTP_DISALLOW_HTTP2_PROXY (1 << 28)

// When set, setup TLS tunnel even when HTTP proxy is used.
// Need to be used together with NS_HTTP_CONNECT_ONLY
#define NS_HTTP_TLS_TUNNEL (1 << 29)

// When set, we use HappyEyeballsConnectionAttempt to establish connection.
#define NS_HTTP_USE_HAPPY_EYEBALLS (1 << 30)

#define NS_HTTP_TRR_FLAGS_FROM_MODE(x) ((static_cast<uint32_t>(x) & 3) << 19)

#define NS_HTTP_TRR_MODE_FROM_FLAGS(x) \
  (static_cast<nsIRequest::TRRMode>((((x) & NS_HTTP_TRR_MODE_MASK) >> 19) & 3))

//-----------------------------------------------------------------------------
// some default values
//-----------------------------------------------------------------------------

#define NS_HTTP_DEFAULT_PORT 80
#define NS_HTTPS_DEFAULT_PORT 443

#define NS_HTTP_HEADER_SEP ','

//-----------------------------------------------------------------------------
// http atoms...
//-----------------------------------------------------------------------------

struct nsHttpAtom;
struct nsHttpAtomLiteral;

namespace nsHttp {
[[nodiscard]] nsresult CreateAtomTable();
void DestroyAtomTable();

// will dynamically add atoms to the table if they don't already exist
nsHttpAtom ResolveAtom(const nsACString& s);

// returns true if the specified token [start,end) is valid per RFC 2616
// section 2.2
bool IsValidToken(const char* start, const char* end);

inline bool IsValidToken(const nsACString& s) {
  return IsValidToken(s.BeginReading(), s.EndReading());
}

// Strip the leading or trailing HTTP whitespace per fetch spec section 2.2.
void TrimHTTPWhitespace(const nsACString& aSource, nsACString& aDest);

// Returns true if the specified value is reasonable given the defintion
// in RFC 2616 section 4.2.  Full strict validation is not performed
// currently as it would require full parsing of the value.
bool IsReasonableHeaderValue(const nsACString& s);

// find the first instance (case-insensitive comparison) of the given
// |token| in the |input| string.  the |token| is bounded by elements of
// |separators| and may appear at the beginning or end of the |input|
// string.  null is returned if the |token| is not found.  |input| may be
// null, in which case null is returned.
const char* FindToken(const char* input, const char* token, const char* seps);

// This function parses a string containing a decimal-valued, non-negative
// 64-bit integer.  If the value would exceed INT64_MAX, then false is
// returned.  Otherwise, this function returns true and stores the
// parsed value in |result|.  The next unparsed character in |input| is
// optionally returned via |next| if |next| is non-null.
//
// TODO(darin): Replace this with something generic.
//
[[nodiscard]] bool ParseInt64(const char* input, const char** next,
                              int64_t* result);

// Variant on ParseInt64 that expects the input string to contain nothing
// more than the value being parsed.
[[nodiscard]] inline bool ParseInt64(const char* input, int64_t* result) {
  const char* next;
  return ParseInt64(input, &next, result) && *next == '\0';
}

// Return whether the HTTP status code represents a permanent redirect
bool IsPermanentRedirect(uint32_t httpStatus);

// Returns the APLN token which represents the used protocol version.
const char* GetProtocolVersion(HttpVersion pv);

bool ValidationRequired(bool isForcedValid,
                        nsHttpResponseHead* cachedResponseHead,
                        uint32_t loadFlags, bool allowStaleCacheContent,
                        bool forceValidateCacheContent, bool isImmutable,
                        bool customConditionalRequest,
                        nsHttpRequestHead& requestHead, nsICacheEntry* entry,
                        CacheControlParser& cacheControlRequest,
                        bool fromPreviousSession,
                        bool* performBackgroundRevalidation = nullptr);

nsresult GetHttpResponseHeadFromCacheEntry(
    nsICacheEntry* entry, nsHttpResponseHead* cachedResponseHead);

nsresult CheckPartial(nsICacheEntry* aEntry, int64_t* aSize,
                      int64_t* aContentLength,
                      nsHttpResponseHead* responseHead);

void DetermineFramingAndImmutability(nsICacheEntry* entry,
                                     nsHttpResponseHead* cachedResponseHead,
                                     bool isHttps, bool* weaklyFramed,
                                     bool* isImmutable);

// Called when an optimization feature affecting active vs background tab load
// took place.  Called only on the parent process and only updates
// mLastActiveTabLoadOptimizationHit timestamp to now.
void NotifyActiveTabLoadOptimization();
TimeStamp GetLastActiveTabLoadOptimizationHit();
void SetLastActiveTabLoadOptimizationHit(TimeStamp const& when);
bool IsBeforeLastActiveTabLoadOptimization(TimeStamp const& when);

nsCString ConvertRequestHeadToString(nsHttpRequestHead& aRequestHead,
                                     bool aHasRequestBody,
                                     bool aRequestBodyHasHeaders,
                                     bool aUsingConnect);

template <typename T>
using SendFunc = std::function<bool(const T&, uint64_t, uint32_t)>;

template <typename T>
bool SendDataInChunks(const nsCString& aData, uint64_t aOffset, uint32_t aCount,
                      const SendFunc<T>& aSendFunc) {
  static uint32_t const kCopyChunkSize = 128 * 1024;
  uint32_t toRead = std::min<uint32_t>(aCount, kCopyChunkSize);

  uint32_t start = 0;
  while (aCount) {
    T data(Substring(aData, start, toRead));

    if (!aSendFunc(data, aOffset, toRead)) {
      return false;
    }

    aOffset += toRead;
    start += toRead;
    aCount -= toRead;
    toRead = std::min<uint32_t>(aCount, kCopyChunkSize);
  }

  return true;
}

}  // namespace nsHttp

struct nsHttpAtomLiteral;
struct nsHttpAtom {
  nsHttpAtom() = default;
  nsHttpAtom(const nsHttpAtom& other) = default;

  explicit operator bool() const { return !_val.IsEmpty(); }

  const char* get() const {
    if (_val.IsEmpty()) {
      return nullptr;
    }
    return _val.get();
  }

  const nsCString& val() const { return _val; }

  void operator=(const nsHttpAtom& a) { _val = a._val; }

  // This constructor is mainly used to build the static atom list
  // Avoid using it for anything else.
  explicit nsHttpAtom(const nsACString& val) : _val(val) {}

 private:
  nsCString _val;
  friend nsHttpAtom nsHttp::ResolveAtom(const nsACString& s);
};

struct nsHttpAtomLiteral {
  const char* get() const { return _data.get(); }
  nsLiteralCString const& val() const { return _data; }

  template <size_t N>
  constexpr explicit nsHttpAtomLiteral(const char (&val)[N]) : _data(val) {}

  operator nsHttpAtom() const { return nsHttpAtom(_data); }

 private:
  nsLiteralCString _data;
};

inline bool operator==(nsHttpAtomLiteral const& self,
                       nsHttpAtomLiteral const& other) {
  return self.get() == other.get();
}
inline bool operator!=(nsHttpAtomLiteral const& self,
                       nsHttpAtomLiteral const& other) {
  return self.get() != other.get();
}

inline bool operator==(nsHttpAtom const& self, nsHttpAtomLiteral const& other) {
  return self.val() == other.val();
}
inline bool operator!=(nsHttpAtom const& self, nsHttpAtomLiteral const& other) {
  return self.val() != other.val();
}

inline bool operator==(nsHttpAtomLiteral const& self, nsHttpAtom const& other) {
  return self.val() == other.val();
}
inline bool operator!=(nsHttpAtomLiteral const& self, nsHttpAtom const& other) {
  return self.val() != other.val();
}

inline bool operator==(nsHttpAtom const& self, nsHttpAtom const& other) {
  return self.val() == other.val();
}
inline bool operator!=(nsHttpAtom const& self, nsHttpAtom const& other) {
  return self.val() != other.val();
}

namespace nsHttp {

// Declare all atoms
//
// The atom names and values are stored in nsHttpAtomList.inc and are brought
// to you by the magic of C preprocessing.  Add new atoms to nsHttpAtomList
// and all support logic will be auto-generated.
//
#define HTTP_ATOM(_name, _value) \
  inline constexpr nsHttpAtomLiteral _name(_value);
#include "nsHttpAtomList.inc"
#undef HTTP_ATOM
}  // namespace nsHttp

//-----------------------------------------------------------------------------
// utilities...
//-----------------------------------------------------------------------------

static inline uint32_t PRTimeToSeconds(PRTime t_usec) {
  return uint32_t(t_usec / PR_USEC_PER_SEC);
}

#define NowInSeconds() PRTimeToSeconds(PR_Now())

// Round q-value to 2 decimal places; return 2 most significant digits as uint.
#define QVAL_TO_UINT(q) ((unsigned int)(((q) + 0.005) * 100.0))

#define HTTP_LWS " \t"
#define HTTP_HEADER_VALUE_SEPS HTTP_LWS ","

void EnsureBuffer(UniquePtr<char[]>& buf, uint32_t newSize, uint32_t preserve,
                  uint32_t& objSize);
void EnsureBuffer(UniquePtr<uint8_t[]>& buf, uint32_t newSize,
                  uint32_t preserve, uint32_t& objSize);

// h2=":443"; ma=60; single
// results in 3 mValues = {{h2, :443}, {ma, 60}, {single}}

class ParsedHeaderPair {
 public:
  ParsedHeaderPair(const char* name, int32_t nameLen, const char* val,
                   int32_t valLen, bool isQuotedValue);

  ParsedHeaderPair(ParsedHeaderPair const& copy)
      : mName(copy.mName),
        mValue(copy.mValue),
        mUnquotedValue(copy.mUnquotedValue),
        mIsQuotedValue(copy.mIsQuotedValue) {
    if (mIsQuotedValue) {
      mValue.Rebind(mUnquotedValue.BeginReading(), mUnquotedValue.Length());
    }
  }

  nsDependentCSubstring mName;
  nsDependentCSubstring mValue;

 private:
  nsCString mUnquotedValue;
  bool mIsQuotedValue;

  void RemoveQuotedStringEscapes(const char* val, int32_t valLen);
};

class ParsedHeaderValueList {
 public:
  ParsedHeaderValueList(const char* t, uint32_t len, bool allowInvalidValue);
  nsTArray<ParsedHeaderPair> mValues;

 private:
  void ParseNameAndValue(const char* input, bool allowInvalidValue);
};

class ParsedHeaderValueListList {
 public:
  // RFC 7231 section 3.2.6 defines the syntax of the header field values.
  // |allowInvalidValue| indicates whether the rule will be used to check
  // the input text.
  // Note that ParsedHeaderValueListList is currently used to parse
  // Alt-Svc and Server-Timing header. |allowInvalidValue| is set to true
  // when parsing Alt-Svc for historical reasons.
  explicit ParsedHeaderValueListList(const nsCString& fullHeader,
                                     bool allowInvalidValue = true);
  nsTArray<ParsedHeaderValueList> mValues;

 private:
  nsCString mFull;
};

void LogHeaders(const char* lineStart);

// Convert HTTP response codes returned by a proxy to nsresult.
// This function should be only used when we get a failed response to the
// CONNECT method.
nsresult HttpProxyResponseToErrorCode(uint32_t aStatusCode);

// Convert an alpn string to SupportedAlpnType.
SupportedAlpnRank IsAlpnSupported(const nsACString& aAlpn);

// Keep this list in sync with the error mapping in
// neqo_glue/src/lib.rs::into_nsresult(). These are the network/NSS errors for
// which we allow a transaction to retry.
static inline bool AllowedErrorForTransactionRetry(nsresult aError) {
  return psm::IsNSSErrorCode(-1 * NS_ERROR_GET_CODE(aError)) ||
         aError == NS_ERROR_NET_RESET ||
         aError == NS_ERROR_CONNECTION_REFUSED ||
         aError == NS_ERROR_UNKNOWN_HOST || aError == NS_ERROR_NET_TIMEOUT ||
         aError == NS_ERROR_NOT_CONNECTED ||
         aError == NS_ERROR_SOCKET_ADDRESS_IN_USE ||
         aError == NS_ERROR_FILE_ALREADY_EXISTS ||
         aError == NS_ERROR_NET_INTERRUPT;
}

[[nodiscard]] nsresult MakeOriginURL(const nsACString& origin,
                                     nsCOMPtr<nsIURI>& url);

[[nodiscard]] nsresult MakeOriginURL(const nsACString& scheme,
                                     const nsACString& origin,
                                     nsCOMPtr<nsIURI>& url);

void CreatePushHashKey(const nsCString& scheme, const nsCString& hostHeader,
                       const mozilla::OriginAttributes& originAttributes,
                       uint64_t serial, const nsACString& pathInfo,
                       nsCString& outOrigin, nsCString& outKey);

// The nsresult carrier (NS_ERROR_WEBTRANSPORT_CODE_BASE..
// NS_ERROR_WEBTRANSPORT_CODE_END) reserves only 256 values, so these two
// stay 8-bit; callers that need the full application-code range (0..2^32-1)
// use WebTransportErrorToHttp3Error()/Http3ErrorToWebTransportError()
// directly, and only clamp for this carrier.
nsresult GetNSResultFromWebTransportError(uint8_t aErrorCode);

uint8_t GetWebTransportErrorFromNSResult(nsresult aResult);

uint64_t WebTransportErrorToHttp3Error(uint32_t aErrorCode);

// Nothing if aErrorCode is not a WebTransport application error code.
mozilla::Maybe<uint32_t> Http3ErrorToWebTransportError(uint64_t aErrorCode);

bool PossibleZeroRTTRetryError(nsresult aReason);

void DisallowHTTPSRR(uint32_t& aCaps);

nsLiteralCString HttpVersionToTelemetryLabel(HttpVersion version);

nsIHttpChannelInternal::ProxyDNSStrategy GetProxyDNSStrategyHelper(
    const char* aType, uint32_t aFlag);

// A null aProxyInfo means the channel is not proxied.
nsIHttpChannelInternal::ProxyType GetProxyTypeHelper(nsIProxyInfo* aProxyInfo);

}  // namespace net
}  // namespace mozilla

#endif  // nsHttp_h_
