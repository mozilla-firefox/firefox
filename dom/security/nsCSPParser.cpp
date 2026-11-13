/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCSPParser.h"

#include <cstdint>
#include <utility>

#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "nsCOMPtr.h"
#include "nsCSPUtils.h"
#include "nsContentUtils.h"
#include "nsIScriptError.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsUnicharUtils.h"

using namespace mozilla;
using namespace mozilla::dom;

static LogModule* GetCspParserLog() {
  static LazyLogModule gCspParserPRLog("CSPParser");
  return gCspParserPRLog;
}

#define CSPPARSERLOG(args) \
  MOZ_LOG(GetCspParserLog(), mozilla::LogLevel::Debug, args)
#define CSPPARSERLOGENABLED() \
  MOZ_LOG_TEST(GetCspParserLog(), mozilla::LogLevel::Debug)

static const uint32_t kSubHostPathCharacterCutoff = 512;

static const char* const kHashSourceValidFns[] = {"sha256", "sha384", "sha512"};

/* ===== nsCSPParser ==================== */

nsCSPParser::nsCSPParser(policyTokens& aTokens, nsIURI* aSelfURI,
                         nsCSPContext* aCSPContext, bool aDeliveredViaMetaTag,
                         bool aSuppressLogMessages)
    : mCurChar(nullptr),
      mEndChar(nullptr),
      mHasHashOrNonce(false),
      mHasAnyUnsafeEval(false),
      mStrictDynamic(false),
      mUnsafeInlineKeywordSrc(nullptr),
      mChildSrc(nullptr),
      mFrameSrc(nullptr),
      mWorkerSrc(nullptr),
      mScriptSrc(nullptr),
      mStyleSrc(nullptr),
      mParsingFrameAncestorsDir(false),
      mTokens(aTokens.Clone()),
      mSelfURI(aSelfURI),
      mPolicy(nullptr),
      mCSPContext(aCSPContext),
      mDeliveredViaMetaTag(aDeliveredViaMetaTag),
      mSuppressLogMessages(aSuppressLogMessages) {
  CSPPARSERLOG(("nsCSPParser::nsCSPParser"));
}

nsCSPParser::~nsCSPParser() { CSPPARSERLOG(("nsCSPParser::~nsCSPParser")); }

static bool isCharacterToken(char16_t aSymbol) {
  return (aSymbol >= 'a' && aSymbol <= 'z') ||
         (aSymbol >= 'A' && aSymbol <= 'Z');
}

bool isNumberToken(char16_t aSymbol) {
  return (aSymbol >= '0' && aSymbol <= '9');
}

bool isValidHexDig(char16_t aHexDig) {
  return (isNumberToken(aHexDig) || (aHexDig >= 'A' && aHexDig <= 'F') ||
          (aHexDig >= 'a' && aHexDig <= 'f'));
}

bool isGroupDelim(char16_t aSymbol) {
  return (aSymbol == '{' || aSymbol == '}' || aSymbol == ',' ||
          aSymbol == '/' || aSymbol == ':' || aSymbol == ';' ||
          aSymbol == '<' || aSymbol == '=' || aSymbol == '>' ||
          aSymbol == '?' || aSymbol == '@' || aSymbol == '[' ||
          aSymbol == '\\' || aSymbol == ']' || aSymbol == '"');
}

bool nsCSPParser::isValidBase64Value(const nsAString& aValue) {
  // Using grammar at
  // https://w3c.github.io/webappsec-csp/#grammardef-base64-value

  const char16_t* cur = aValue.BeginReading();
  const char16_t* end = aValue.EndReading();

  // May end with one or two =
  if (end > cur && *(end - 1) == EQUALS) end--;
  if (end > cur && *(end - 1) == EQUALS) end--;

  // Must have at least one character aside from any =
  if (end == cur) {
    return false;
  }

  // Rest must all be A-Za-z0-9+/-_
  for (; cur < end; ++cur) {
    if (!(isCharacterToken(*cur) || isNumberToken(*cur) || *cur == PLUS ||
          *cur == SLASH || *cur == DASH || *cur == UNDERLINE)) {
      return false;
    }
  }

  return true;
}

void nsCSPParser::resetCurChar(const nsAString& aToken) {
  mCurChar = aToken.BeginReading();
  mEndChar = aToken.EndReading();
  resetCurValue();
}

// The path is terminated by the first question mark ("?") or
// number sign ("#") character, or by the end of the URI.
// http://tools.ietf.org/html/rfc3986#section-3.3
bool nsCSPParser::atEndOfPath() {
  return (atEnd() || peek(QUESTIONMARK) || peek(NUMBER_SIGN));
}

// unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
bool nsCSPParser::atValidUnreservedChar() {
  return (peek(isCharacterToken) || peek(isNumberToken) || peek(DASH) ||
          peek(DOT) || peek(UNDERLINE) || peek(TILDE));
}

// sub-delims    = "!" / "$" / "&" / "'" / "(" / ")"
//                 / "*" / "+" / "," / ";" / "="
// Please note that even though ',' and ';' appear to be
// valid sub-delims according to the RFC production of paths,
// both can not appear here by itself, they would need to be
// pct-encoded in order to be part of the path.
bool nsCSPParser::atValidSubDelimChar() {
  return (peek(EXCLAMATION) || peek(DOLLAR) || peek(AMPERSAND) ||
          peek(SINGLEQUOTE) || peek(OPENBRACE) || peek(CLOSINGBRACE) ||
          peek(WILDCARD) || peek(PLUS) || peek(EQUALS));
}

// pct-encoded   = "%" HEXDIG HEXDIG
bool nsCSPParser::atValidPctEncodedChar() {
  const char16_t* pctCurChar = mCurChar;

  if ((pctCurChar + 2) >= mEndChar) {
    // string too short, can't be a valid pct-encoded char.
    return false;
  }

  // Any valid pct-encoding must follow the following format:
  // "% HEXDIG HEXDIG"
  if (PERCENT_SIGN != *pctCurChar || !isValidHexDig(*(pctCurChar + 1)) ||
      !isValidHexDig(*(pctCurChar + 2))) {
    return false;
  }
  return true;
}

// pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
// http://tools.ietf.org/html/rfc3986#section-3.3
bool nsCSPParser::atValidPathChar() {
  return (atValidUnreservedChar() || atValidSubDelimChar() ||
          atValidPctEncodedChar() || peek(COLON) || peek(ATSYMBOL));
}

void nsCSPParser::logWarningErrorToConsole(uint32_t aSeverityFlag,
                                           const char* aProperty,
                                           const nsTArray<nsString>& aParams) {
  CSPPARSERLOG(("nsCSPParser::logWarningErrorToConsole: %s", aProperty));

  if (mSuppressLogMessages) {
    return;
  }

  // send console messages off to the context and let the context
  // deal with it (potentially messages need to be queued up)
  mCSPContext->logToConsole(aProperty, aParams,
                            ""_ns,           // aSourceName
                            u""_ns,          // aSourceLine
                            0,               // aLineNumber
                            1,               // aColumnNumber
                            aSeverityFlag);  // aFlags
}

bool nsCSPParser::hostChar() {
  if (atEnd()) {
    return false;
  }
  return accept(isCharacterToken) || accept(isNumberToken) || accept(DASH);
}

// (ALPHA / DIGIT / "+" / "-" / "." )
bool nsCSPParser::schemeChar() {
  if (atEnd()) {
    return false;
  }
  return accept(isCharacterToken) || accept(isNumberToken) || accept(PLUS) ||
         accept(DASH) || accept(DOT);
}

// ":" port-part, where port-part = 1*DIGIT / "*"
bool nsCSPParser::port() {
  CSPPARSERLOG(("nsCSPParser::port, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Consume the COLON we just peeked at in houstSource
  accept(COLON);

  // Resetting current value since we start to parse a port now.
  // e.g; "http://www.example.com:8888" then we have already parsed
  // everything up to (including) ":";
  resetCurValue();

  // Port might be "*"
  if (accept(WILDCARD)) {
    return true;
  }

  // Port must start with a number
  if (!accept(isNumberToken)) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag, "couldntParsePort",
                             params);
    return false;
  }
  // Consume more numbers and set parsed port to the nsCSPHost
  while (accept(isNumberToken)) { /* consume */
  }
  return true;
}

bool nsCSPParser::subPath(nsCSPHostSrc* aCspHost) {
  CSPPARSERLOG(("nsCSPParser::subPath, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Emergency exit to avoid endless loops in case a path in a CSP policy
  // is longer than 512 characters, or also to avoid endless loops
  // in case we are parsing unrecognized characters in the following loop.
  uint32_t charCounter = 0;
  nsString pctDecodedSubPath;

  while (!atEndOfPath()) {
    if (peek(SLASH)) {
      // before appendig any additional portion of a subpath we have to
      // pct-decode that portion of the subpath. atValidPathChar() already
      // verified a correct pct-encoding, now we can safely decode and append
      // the decoded-sub path.
      CSP_PercentDecodeStr(mCurValue, pctDecodedSubPath);
      aCspHost->appendPath(pctDecodedSubPath);
      // Resetting current value since we are appending parts of the path
      // to aCspHost, e.g; "http://www.example.com/path1/path2" then the
      // first part is "/path1", second part "/path2"
      resetCurValue();
    } else if (!atValidPathChar()) {
      AutoTArray<nsString, 1> params = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "couldntParseInvalidSource", params);
      return false;
    }
    // potentially we have encountred a valid pct-encoded character in
    // atValidPathChar(); if so, we have to account for "% HEXDIG HEXDIG" and
    // advance the pointer past the pct-encoded char.
    if (peek(PERCENT_SIGN)) {
      advance();
      advance();
    }
    advance();
    if (++charCounter > kSubHostPathCharacterCutoff) {
      return false;
    }
  }
  // before appendig any additional portion of a subpath we have to pct-decode
  // that portion of the subpath. atValidPathChar() already verified a correct
  // pct-encoding, now we can safely decode and append the decoded-sub path.
  CSP_PercentDecodeStr(mCurValue, pctDecodedSubPath);
  aCspHost->appendPath(pctDecodedSubPath);
  resetCurValue();
  return true;
}

bool nsCSPParser::path(nsCSPHostSrc* aCspHost) {
  CSPPARSERLOG(("nsCSPParser::path, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Resetting current value and forgetting everything we have parsed so far
  // e.g. parsing "http://www.example.com/path1/path2", then
  // "http://www.example.com" has already been parsed so far
  // forget about it.
  resetCurValue();

  if (!accept(SLASH)) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "couldntParseInvalidSource", params);
    return false;
  }
  if (atEndOfPath()) {
    // one slash right after host [port] is also considered a path, e.g.
    // www.example.com/ should result in www.example.com/
    // please note that we do not have to perform any pct-decoding here
    // because we are just appending a '/' and not any actual chars.
    aCspHost->appendPath(u"/"_ns);
    return true;
  }
  // path can begin with "/" but not "//"
  // see http://tools.ietf.org/html/rfc3986#section-3.3
  if (peek(SLASH)) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "couldntParseInvalidSource", params);
    return false;
  }
  return subPath(aCspHost);
}

bool nsCSPParser::subHost() {
  CSPPARSERLOG(("nsCSPParser::subHost, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Emergency exit to avoid endless loops in case a host in a CSP policy
  // is longer than 512 characters, or also to avoid endless loops
  // in case we are parsing unrecognized characters in the following loop.
  uint32_t charCounter = 0;

  while (!atEndOfPath() && !peek(COLON) && !peek(SLASH)) {
    ++charCounter;
    while (hostChar()) {
      /* consume */
      ++charCounter;
    }
    if (accept(DOT) && !hostChar()) {
      return false;
    }
    if (charCounter > kSubHostPathCharacterCutoff) {
      return false;
    }
  }
  return true;
}

// host-part = "*" / [ "*." ] 1*host-char *( "." 1*host-char ) [ "." ]
//
// Non-standard: A trailing "." isn't supported.
nsCSPHostSrc* nsCSPParser::host() {
  CSPPARSERLOG(("nsCSPParser::host, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Check if the token starts with "*"; please remember that we handle
  // a single "*" as host in sourceExpression, but we still have to handle
  // the case where a scheme was defined, e.g., as:
  // "https://*", "*.example.com", "*:*", etc.
  if (accept(WILDCARD)) {
    // Might solely be the wildcard
    if (atEnd() || peek(COLON)) {
      return new nsCSPHostSrc(mCurValue);
    }
    // If the token is not only the "*", a "." must follow right after
    if (!accept(DOT)) {
      AutoTArray<nsString, 1> params = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "couldntParseInvalidHost", params);
      return nullptr;
    }
  }

  // Expecting at least one host-char
  if (!hostChar()) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "couldntParseInvalidHost", params);
    return nullptr;
  }

  // There might be several sub hosts defined.
  if (!subHost()) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "couldntParseInvalidHost", params);
    return nullptr;
  }

  // HostName might match a keyword, log to the console.
  if (CSP_IsQuotelessKeyword(mCurValue)) {
    nsString keyword = mCurValue;
    ToLowerCase(keyword);
    AutoTArray<nsString, 2> params = {mCurToken, std::move(keyword)};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "hostNameMightBeKeyword", params);
  }

  // Create a new nsCSPHostSrc with the parsed host.
  return new nsCSPHostSrc(mCurValue);
}

// keyword-source = "'self'" / "'unsafe-inline'" / "'unsafe-eval'"
//                  / "'strict-dynamic'" / "'unsafe-hashes'"
//                  / "'report-sample'" / "'unsafe-allow-redirects'"
//                  / "'wasm-unsafe-eval'" / "'trusted-types-eval'"
//                  / "'report-sha256'" / "'report-sha384'"
//                  / "'report-sha512'" / "'unsafe-webtransport-hashes'"
//
// Non-standard: 'unsafe-allow-redirects', 'report-sha256', 'report-sha384',
// 'report-sha512' and 'unsafe-webtransport-hashes' aren't supported.
nsCSPBaseSrc* nsCSPParser::keywordSource() {
  CSPPARSERLOG(("nsCSPParser::keywordSource, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Special case handling for 'self' which is not stored internally as a
  // keyword, but rather creates a nsCSPHostSrc using the selfURI
  if (CSP_IsKeyword(mCurToken, CSP_SELF)) {
    return CSP_CreateHostSrcFromSelfURI(mSelfURI);
  }

  if (CSP_IsKeyword(mCurToken, CSP_REPORT_SAMPLE)) {
    return new nsCSPKeywordSrc(CSP_UTF16KeywordToEnum(mCurToken));
  }

  if (CSP_IsKeyword(mCurToken, CSP_STRICT_DYNAMIC)) {
    if (!CSP_IsDirective(mCurDir[0],
                         nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE) &&
        !CSP_IsDirective(mCurDir[0],
                         nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE) &&
        !CSP_IsDirective(mCurDir[0],
                         nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE) &&
        !CSP_IsDirective(mCurDir[0],
                         nsIContentSecurityPolicy::DEFAULT_SRC_DIRECTIVE)) {
      AutoTArray<nsString, 1> params = {u"strict-dynamic"_ns};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "ignoringStrictDynamic", params);
    }

    mStrictDynamic = true;
    return new nsCSPKeywordSrc(CSP_UTF16KeywordToEnum(mCurToken));
  }

  if (CSP_IsKeyword(mCurToken, CSP_UNSAFE_INLINE)) {
    // make sure script-src only contains 'unsafe-inline' once;
    // ignore duplicates and log warning
    if (mUnsafeInlineKeywordSrc) {
      AutoTArray<nsString, 1> params = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "ignoringDuplicateSrc", params);
      return nullptr;
    }
    // cache if we encounter 'unsafe-inline' so we can invalidate (ignore) it in
    // case that script-src directive also contains hash- or nonce-.
    mUnsafeInlineKeywordSrc =
        new nsCSPKeywordSrc(CSP_UTF16KeywordToEnum(mCurToken));
    return mUnsafeInlineKeywordSrc;
  }

  if (CSP_IsKeyword(mCurToken, CSP_UNSAFE_EVAL)) {
    mHasAnyUnsafeEval = true;
    return new nsCSPKeywordSrc(CSP_UTF16KeywordToEnum(mCurToken));
  }

  if (CSP_IsKeyword(mCurToken, CSP_WASM_UNSAFE_EVAL)) {
    mHasAnyUnsafeEval = true;
    return new nsCSPKeywordSrc(CSP_UTF16KeywordToEnum(mCurToken));
  }

  if (CSP_IsKeyword(mCurToken, CSP_UNSAFE_HASHES)) {
    return new nsCSPKeywordSrc(CSP_UTF16KeywordToEnum(mCurToken));
  }

  if (StaticPrefs::dom_security_trusted_types_enabled() &&
      CSP_IsKeyword(mCurToken, CSP_TRUSTED_TYPES_EVAL)) {
    return new nsCSPKeywordSrc(CSP_UTF16KeywordToEnum(mCurToken));
  }

  return nullptr;
}

// host-source = [ scheme-part "://" ] host-part [ ":" port-part ]
//               [ path-part ]
nsCSPHostSrc* nsCSPParser::hostSource() {
  CSPPARSERLOG(("nsCSPParser::hostSource, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  nsCSPHostSrc* cspHost = host();
  if (!cspHost) {
    // Error was reported in host()
    return nullptr;
  }

  // Calling port() to see if there is a port to parse, if an error
  // occurs, port() reports the error, if port() returns true;
  // we have a valid port, so we add it to cspHost.
  if (peek(COLON)) {
    if (!port()) {
      delete cspHost;
      return nullptr;
    }
    cspHost->setPort(mCurValue);
  }

  if (atEndOfPath()) {
    return cspHost;
  }

  // Calling path() to see if there is a path to parse, if an error
  // occurs, path() reports the error; handing cspHost as an argument
  // which simplifies parsing of several paths.
  if (!path(cspHost)) {
    // If the host [port] is followed by a path, it has to be a valid path,
    // otherwise we pass the nullptr, indicating an error, up the callstack.
    // see also https://w3c.github.io/webappsec-csp/#grammardef-path-part
    delete cspHost;
    return nullptr;
  }
  return cspHost;
}

// scheme-source = scheme-part ":"
nsCSPSchemeSrc* nsCSPParser::schemeSource() {
  CSPPARSERLOG(("nsCSPParser::schemeSource, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  if (!accept(isCharacterToken)) {
    return nullptr;
  }
  while (schemeChar()) { /* consume */
  }
  nsString scheme = mCurValue;

  // If the potential scheme is not followed by ":" - it's not a scheme
  if (!accept(COLON)) {
    return nullptr;
  }

  // If the chraracter following the ":" is a number or the "*"
  // then we are not parsing a scheme; but rather a host;
  if (peek(isNumberToken) || peek(WILDCARD)) {
    return nullptr;
  }

  return new nsCSPSchemeSrc(scheme);
}

// nonce-source = "'nonce-" base64-value "'"
nsCSPNonceSrc* nsCSPParser::nonceSource() {
  CSPPARSERLOG(("nsCSPParser::nonceSource, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Check if mCurToken begins with "'nonce-" and ends with "'"
  if (!StringBeginsWith(mCurToken,
                        nsDependentString(CSP_EnumToUTF16Keyword(CSP_NONCE)),
                        nsASCIICaseInsensitiveStringComparator) ||
      mCurToken.Last() != SINGLEQUOTE) {
    return nullptr;
  }

  // Trim surrounding single quotes
  const nsAString& expr = Substring(mCurToken, 1, mCurToken.Length() - 2);

  int32_t dashIndex = expr.FindChar(DASH);
  if (dashIndex < 0) {
    return nullptr;
  }
  if (!isValidBase64Value(Substring(expr, dashIndex + 1))) {
    return nullptr;
  }

  // cache if encountering hash or nonce to invalidate unsafe-inline
  mHasHashOrNonce = true;
  return new nsCSPNonceSrc(Substring(expr, dashIndex + 1));
}

static bool ExtractAlgorithmAndHash(const nsAString& aExpr,
                                    nsAString& aOutAlgorithm,
                                    nsAString& aOutHash) {
  int32_t dashIndex = aExpr.FindChar(DASH);
  if (dashIndex < 0) {
    return false;
  }

  const nsAString& algorithm = Substring(aExpr, 0, dashIndex);
  const nsAString& hash = Substring(aExpr, dashIndex + 1);

  if (!nsCSPParser::isValidBase64Value(hash)) {
    return false;
  }

  for (auto hashSourceValidFn : kHashSourceValidFns) {
    if (algorithm.LowerCaseEqualsASCII(hashSourceValidFn)) {
      aOutAlgorithm = algorithm;
      aOutHash = hash;
      return true;
    }
  }

  return false;
}

// hash-source = "'" hash-algorithm "-" base64-value "'"
nsCSPHashSrc* nsCSPParser::hashSource() {
  CSPPARSERLOG(("nsCSPParser::hashSource, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Check if mCurToken starts and ends with "'"
  if (mCurToken.First() != SINGLEQUOTE || mCurToken.Last() != SINGLEQUOTE) {
    return nullptr;
  }

  // Trim surrounding single quotes
  const nsAString& expr = Substring(mCurToken, 1, mCurToken.Length() - 2);

  nsAutoString algorithm;
  nsAutoString hash;
  if (!ExtractAlgorithmAndHash(expr, algorithm, hash)) {
    return nullptr;
  }

  // Cache if encountering hash or nonce to invalidate unsafe-inline
  mHasHashOrNonce = true;

  return new nsCSPHashSrc(algorithm, hash);
}

// source-expression = scheme-source / host-source / keyword-source
//                     / nonce-source / hash-source
nsCSPBaseSrc* nsCSPParser::sourceExpression() {
  CSPPARSERLOG(("nsCSPParser::sourceExpression, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Check if it is a keyword
  if (nsCSPBaseSrc* cspKeyword = keywordSource()) {
    return cspKeyword;
  }

  // Check if it is a nonce-source
  if (nsCSPNonceSrc* cspNonce = nonceSource()) {
    return cspNonce;
  }

  // Check if it is a hash-source
  if (nsCSPHashSrc* cspHash = hashSource()) {
    return cspHash;
  }

  // We handle a single "*" as host here, to avoid any confusion when applying
  // the default scheme. However, we still would need to apply the default
  // scheme in case we would parse "*:80".
  if (mCurToken.EqualsASCII("*")) {
    return new nsCSPHostSrc(u"*"_ns);
  }

  // Calling resetCurChar allows us to use mCurChar and mEndChar
  // to parse mCurToken; e.g. mCurToken = "http://www.example.com", then
  // mCurChar = 'h'
  // mEndChar = points just after the last 'm'
  // mCurValue = ""
  resetCurChar(mCurToken);

  // Check if mCurToken starts with a scheme
  nsAutoString parsedScheme;
  if (nsCSPSchemeSrc* cspScheme = schemeSource()) {
    // mCurToken might only enforce a specific scheme
    if (atEnd()) {
      return cspScheme;
    }
    // If something follows the scheme, we do not create
    // a nsCSPSchemeSrc, but rather a nsCSPHostSrc, which
    // needs to know the scheme to enforce; remember the
    // scheme and delete cspScheme;
    cspScheme->toString(parsedScheme);
    parsedScheme.Trim(":", false, true);
    delete cspScheme;

    // If mCurToken provides not only a scheme, but also a host, we have to
    // check if two slashes follow the scheme.
    if (!accept(SLASH) || !accept(SLASH)) {
      AutoTArray<nsString, 1> params = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "failedToParseUnrecognizedSource", params);
      return nullptr;
    }
  }

  // Calling resetCurValue allows us to keep pointers for mCurChar and mEndChar
  // alive, but resets mCurValue; e.g. mCurToken = "http://www.example.com",
  // then mCurChar = 'w' mEndChar = 'm' mCurValue = ""
  resetCurValue();

  // If mCurToken does not provide a scheme (scheme-less source), we apply the
  // scheme from selfURI
  if (parsedScheme.IsEmpty()) {
    // Resetting internal helpers, because we might already have parsed some of
    // the host when trying to parse a scheme.
    resetCurChar(mCurToken);
    nsAutoCString selfScheme;
    mSelfURI->GetScheme(selfScheme);
    parsedScheme.AssignASCII(selfScheme.get());
  }

  // At this point we are expecting a host to be parsed.
  // Trying to create a new nsCSPHost.
  if (nsCSPHostSrc* cspHost = hostSource()) {
    // Do not forget to set the parsed scheme.
    cspHost->setScheme(parsedScheme);
    cspHost->setWithinFrameAncestorsDir(mParsingFrameAncestorsDir);
    return cspHost;
  }
  // Error was reported in hostSource()
  return nullptr;
}

void nsCSPParser::logWarningForIgnoringNoneKeywordToConsole() {
  AutoTArray<nsString, 1> params;
  params.AppendElement(CSP_EnumToUTF16Keyword(CSP_NONE));
  logWarningErrorToConsole(nsIScriptError::warningFlag, "ignoringUnknownOption",
                           params);
}

// https://w3c.github.io/webappsec-csp/#framework-directive-source-list
// serialized-source-list = ( source-expression *( required-ascii-whitespace
//                            source-expression ) ) / "'none'"
//
// Unlike the grammar, this also accepts an empty source list and 'none' next
// to other source expressions, as the spec's algorithms do, see the notes in
// step 3 of https://w3c.github.io/webappsec-csp/#match-url-to-source-list
// and https://github.com/w3c/webappsec-csp/issues/411.
void nsCSPParser::sourceList(nsTArray<nsCSPBaseSrc*>& outSrcs) {
  bool isNone = false;

  // remember, srcs start at index 1
  for (uint32_t i = 1; i < mCurDir.Length(); i++) {
    // mCurToken is only set here and remains the current token
    // to be processed, which avoid passing arguments between functions.
    mCurToken = mCurDir[i];
    resetCurValue();

    CSPPARSERLOG(("nsCSPParser::sourceList, mCurToken: %s, mCurValue: %s",
                  NS_ConvertUTF16toUTF8(mCurToken).get(),
                  NS_ConvertUTF16toUTF8(mCurValue).get()));

    // Special case handling for none:
    // Ignore 'none' if any other src is available.
    // (See the note in step 3 of
    // https://w3c.github.io/webappsec-csp/#match-url-to-source-list)
    if (CSP_IsKeyword(mCurToken, CSP_NONE)) {
      isNone = true;
      continue;
    }
    // Must be a regular source expression
    nsCSPBaseSrc* src = sourceExpression();
    if (src) {
      outSrcs.AppendElement(src);
    }
  }

  // Check if the directive contains a 'none'
  if (isNone) {
    // If the directive contains no other srcs, then we set the 'none'
    if (outSrcs.IsEmpty() ||
        (outSrcs.Length() == 1 && outSrcs[0]->isReportSample())) {
      nsCSPKeywordSrc* keyword = new nsCSPKeywordSrc(CSP_NONE);
      outSrcs.InsertElementAt(0, keyword);
    }
    // Otherwise, we ignore 'none' and report a warning
    else {
      logWarningForIgnoringNoneKeywordToConsole();
    }
  }
}

void nsCSPParser::reportURIList(nsCSPDirective* aDir) {
  CSPPARSERLOG(("nsCSPParser::reportURIList"));

  nsTArray<nsCSPBaseSrc*> srcs;
  nsCOMPtr<nsIURI> uri;
  nsresult rv;

  // remember, srcs start at index 1
  for (uint32_t i = 1; i < mCurDir.Length(); i++) {
    mCurToken = mCurDir[i];

    CSPPARSERLOG(("nsCSPParser::reportURIList, mCurToken: %s, mCurValue: %s",
                  NS_ConvertUTF16toUTF8(mCurToken).get(),
                  NS_ConvertUTF16toUTF8(mCurValue).get()));

    rv = NS_NewURI(getter_AddRefs(uri), mCurToken, "", mSelfURI);

    // If creating the URI casued an error, skip this URI
    if (NS_FAILED(rv)) {
      AutoTArray<nsString, 1> params = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "couldNotParseReportURI", params);
      continue;
    }

    // Create new nsCSPReportURI and append to the list.
    nsCSPReportURI* reportURI = new nsCSPReportURI(uri);
    srcs.AppendElement(reportURI);
  }

  if (srcs.Length() == 0) {
    AutoTArray<nsString, 1> directiveName = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "ignoringDirectiveWithNoValues", directiveName);
    delete aDir;
    return;
  }

  aDir->addSrcs(srcs);
  mPolicy->addDirective(aDir);
}

void nsCSPParser::reportGroup(nsCSPDirective* aDir) {
  CSPPARSERLOG(("nsCSPParser::reportGroup"));

  if (mCurDir.Length() < 2) {
    AutoTArray<nsString, 1> directiveName = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "ignoringDirectiveWithNoValues", directiveName);
    delete aDir;
    return;
  }

  nsTArray<nsCSPBaseSrc*> srcs;
  mCurToken = mCurDir[1];

  CSPPARSERLOG(("nsCSPParser::reportGroup, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  resetCurChar(mCurToken);
  while (!atEnd()) {
    if (isGroupDelim(*mCurChar) ||
        nsContentUtils::IsHTMLWhitespace(*mCurChar)) {
      nsString badChar(mozilla::Span(mCurChar, 1));
      AutoTArray<nsString, 2> params = {mCurToken, std::move(badChar)};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "ignoringInvalidGroupSyntax", params);
      delete aDir;
      return;
    }
    advance();
  }

  nsCSPGroup* group = new nsCSPGroup(mCurToken);
  srcs.AppendElement(group);
  aDir->addSrcs(srcs);
  mPolicy->addDirective(aDir);
  aDir = nullptr;
};

/* Helper function for parsing sandbox flags. This function solely concatenates
 * all the source list tokens (the sandbox flags) so the attribute parser
 * (nsContentUtils::ParseSandboxAttributeToFlags) can parse them.
 */
void nsCSPParser::sandboxFlagList(nsCSPDirective* aDir) {
  CSPPARSERLOG(("nsCSPParser::sandboxFlagList"));

  nsAutoString flags;

  // remember, srcs start at index 1
  for (uint32_t i = 1; i < mCurDir.Length(); i++) {
    mCurToken = mCurDir[i];

    CSPPARSERLOG(("nsCSPParser::sandboxFlagList, mCurToken: %s, mCurValue: %s",
                  NS_ConvertUTF16toUTF8(mCurToken).get(),
                  NS_ConvertUTF16toUTF8(mCurValue).get()));

    if (!nsContentUtils::IsValidSandboxFlag(mCurToken)) {
      AutoTArray<nsString, 1> params = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "couldntParseInvalidSandboxFlag", params);
      continue;
    }

    flags.Append(mCurToken);
    if (i != mCurDir.Length() - 1) {
      flags.AppendLiteral(" ");
    }
  }

  // Please note that the sandbox directive can exist
  // by itself (not containing any flags).
  nsTArray<nsCSPBaseSrc*> srcs;
  srcs.AppendElement(new nsCSPSandboxFlags(flags));
  aDir->addSrcs(srcs);
  mPolicy->addDirective(aDir);
}

static bool IsValidRequireTrustedTypesForDirectiveValue(
    const nsAString& aToken) {
  return aToken.Equals(kValidRequireTrustedTypesForDirectiveValue);
}

void nsCSPParser::handleRequireTrustedTypesForDirective(nsCSPDirective* aDir) {
  CSPPARSERLOG(("nsCSPParser::handleTrustedTypesDirective"));

  // "srcs" start at index 1. Here "srcs" should represent Trusted Types' sink
  // groups
  // (https://w3c.github.io/trusted-types/dist/spec/#require-trusted-types-for-csp-directive).
  // We align with other browsers and make the syntax forgiving i.e. invalid
  // trusted-types-sink-group-keyword are discarded without invalidating the
  // whole directive. However, if no valid trusted-types-sink-group-keyword is
  // found, the directive has no effect and can just be discarded completely.
  // See https://github.com/w3c/trusted-types/issues/580.
  if (mCurDir.Length() < 2) {
    nsString numberOfTokensStr;

    // Casting is required to avoid ambiguous function calls on some platforms.
    numberOfTokensStr.AppendInt(static_cast<uint64_t>(mCurDir.Length()));

    AutoTArray<nsString, 1> numberOfTokensArr = {std::move(numberOfTokensStr)};
    logWarningErrorToConsole(nsIScriptError::errorFlag,
                             "invalidNumberOfTrustedTypesForDirectiveValues",
                             numberOfTokensArr);
    delete aDir;
    return;
  }

  nsTArray<nsCSPBaseSrc*> trustedTypesSinkGroupKeywords;
  bool foundValidTrustedTypesSinkGroupKeyword = false;
  for (uint32_t i = 1; i < mCurDir.Length(); ++i) {
    mCurToken = mCurDir[i];

    CSPPARSERLOG(
        ("nsCSPParser::handleRequireTrustedTypesForDirective, mCurToken: %s",
         NS_ConvertUTF16toUTF8(mCurToken).get()));

    if (!IsValidRequireTrustedTypesForDirectiveValue(mCurToken)) {
      AutoTArray<nsString, 1> token = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "invalidRequireTrustedTypesForDirectiveValue",
                               token);
    } else {
      foundValidTrustedTypesSinkGroupKeyword = true;
    }
    trustedTypesSinkGroupKeywords.AppendElement(
        new nsCSPRequireTrustedTypesForDirectiveValue(mCurToken));
  }
  if (!foundValidTrustedTypesSinkGroupKeyword) {
    for (auto* trustedTypesSinkGroupKeyword : trustedTypesSinkGroupKeywords) {
      delete trustedTypesSinkGroupKeyword;
    }
    delete aDir;
    return;
  }
  aDir->addSrcs(trustedTypesSinkGroupKeywords);
  mPolicy->addDirective(aDir);
}

// https://w3c.github.io/trusted-types/dist/spec/#trusted-types-csp-directive
static bool IsValidTrustedTypesWildcard(const nsAString& aToken) {
  // tt-wildcard = "*"
  return aToken.Length() == 1 && aToken.First() == WILDCARD;
}

static bool IsValidTrustedTypesPolicyNameChar(char16_t aChar) {
  // tt-policy-name = 1*( ALPHA / DIGIT / "-" / "#" / "=" / "_" / "/" / "@" /
  // "." / "%")
  return nsContentUtils::IsAlphanumeric(aChar) || aChar == DASH ||
         aChar == NUMBER_SIGN || aChar == EQUALS || aChar == UNDERLINE ||
         aChar == SLASH || aChar == ATSYMBOL || aChar == DOT ||
         aChar == PERCENT_SIGN;
}

// https://w3c.github.io/trusted-types/dist/spec/#trusted-types-csp-directive
static bool IsValidTrustedTypesPolicyName(const nsAString& aToken) {
  // tt-policy-name = 1*( ALPHA / DIGIT / "-" / "#" / "=" / "_" / "/" / "@" /
  // "." / "%")

  if (aToken.IsEmpty()) {
    return false;
  }

  for (uint32_t i = 0; i < aToken.Length(); ++i) {
    if (!IsValidTrustedTypesPolicyNameChar(aToken.CharAt(i))) {
      return false;
    }
  }

  return true;
}

void nsCSPParser::handleTrustedTypesDirective(nsCSPDirective* aDir) {
  CSPPARSERLOG(("nsCSPParser::handleTrustedTypesDirective"));

  nsTArray<nsCSPBaseSrc*> trustedTypesExpressions;

  bool containsKeywordNone = false;

  // "srcs" start and index 1. Here they should represent the tt-expressions
  // (https://w3c.github.io/trusted-types/dist/spec/#trusted-types-csp-directive).
  for (uint32_t i = 1; i < mCurDir.Length(); ++i) {
    mCurToken = mCurDir[i];

    CSPPARSERLOG(("nsCSPParser::handleTrustedTypesDirective, mCurToken: %s",
                  NS_ConvertUTF16toUTF8(mCurToken).get()));

    // tt-expression = tt-policy-name  / tt-keyword / tt-wildcard
    if (IsValidTrustedTypesPolicyName(mCurToken)) {
      trustedTypesExpressions.AppendElement(
          new nsCSPTrustedTypesDirectivePolicyName(mCurToken));
    } else if (CSP_IsKeyword(mCurToken, CSP_NONE)) {
      containsKeywordNone = true;
    } else if (CSP_IsKeyword(mCurToken, CSP_ALLOW_DUPLICATES)) {
      trustedTypesExpressions.AppendElement(
          new nsCSPKeywordSrc(CSP_ALLOW_DUPLICATES));
    } else if (IsValidTrustedTypesWildcard(mCurToken)) {
      trustedTypesExpressions.AppendElement(
          new nsCSPTrustedTypesDirectivePolicyName(mCurToken));
    } else {
      AutoTArray<nsString, 1> token = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "invalidTrustedTypesExpression", token);
      trustedTypesExpressions.AppendElement(
          new nsCSPTrustedTypesDirectiveInvalidToken(mCurToken));
    }
  }

  if (trustedTypesExpressions.IsEmpty()) {
    // No tt-expression is equivalent to 'none', see
    // <https://w3c.github.io/trusted-types/dist/spec/#trusted-types-csp-directive>.
    trustedTypesExpressions.AppendElement(new nsCSPKeywordSrc(CSP_NONE));
  } else if (containsKeywordNone) {
    // See step 2.4's note at
    // <https://w3c.github.io/trusted-types/dist/spec/#should-block-create-policy>.
    logWarningForIgnoringNoneKeywordToConsole();
  }

  aDir->addSrcs(trustedTypesExpressions);
  mPolicy->addDirective(aDir);
}

// directive-value = *( WSP / <VCHAR except ";" and ","> )
void nsCSPParser::directiveValue(nsTArray<nsCSPBaseSrc*>& outSrcs) {
  CSPPARSERLOG(("nsCSPParser::directiveValue"));

  // Just forward to sourceList
  sourceList(outSrcs);
}

// directive-name = 1*( ALPHA / DIGIT / "-" )
nsCSPDirective* nsCSPParser::directiveName() {
  CSPPARSERLOG(("nsCSPParser::directiveName, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  // Check if it is a valid directive
  CSPDirective directive = CSP_StringToCSPDirective(mCurToken);
  if (directive == nsIContentSecurityPolicy::NO_DIRECTIVE ||
      (!StaticPrefs::dom_security_trusted_types_enabled() &&
       (directive ==
            nsIContentSecurityPolicy::REQUIRE_TRUSTED_TYPES_FOR_DIRECTIVE ||
        directive == nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE))) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "couldNotProcessUnknownDirective", params);
    return nullptr;
  }

  // The directive 'reflected-xss' is part of CSP 1.1, see:
  // http://www.w3.org/TR/2014/WD-CSP11-20140211/#reflected-xss
  // Currently we are not supporting that directive, hence we log a
  // warning to the console and ignore the directive including its values.
  if (directive == nsIContentSecurityPolicy::REFLECTED_XSS_DIRECTIVE) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "notSupportingDirective", params);
    return nullptr;
  }

  // Make sure the directive does not already exist
  // (see http://www.w3.org/TR/CSP11/#parsing)
  if (mPolicy->hasDirective(directive)) {
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag, "duplicateDirective",
                             params);
    return nullptr;
  }

  // CSP delivered via meta tag should ignore the following directives:
  // report-uri, frame-ancestors, and sandbox, see:
  // http://www.w3.org/TR/CSP11/#delivery-html-meta-element
  if (mDeliveredViaMetaTag &&
      ((directive == nsIContentSecurityPolicy::REPORT_URI_DIRECTIVE) ||
       (directive == nsIContentSecurityPolicy::FRAME_ANCESTORS_DIRECTIVE) ||
       (directive == nsIContentSecurityPolicy::SANDBOX_DIRECTIVE))) {
    // log to the console to indicate that meta CSP is ignoring the directive
    AutoTArray<nsString, 1> params = {mCurToken};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "ignoringSrcFromMetaCSP", params);
    return nullptr;
  }

  // special case handling for block-all-mixed-content
  if (directive == nsIContentSecurityPolicy::BLOCK_ALL_MIXED_CONTENT) {
    // If mixed content upgrade is enabled for display content, then
    // block-all-mixed-content is obsolete.
    if (mozilla::StaticPrefs::
            security_mixed_content_upgrade_display_content()) {
      // log to the console that if mixed content display upgrading is enabled
      // block-all-mixed-content is obsolete.
      AutoTArray<nsString, 1> params = {mCurToken};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "obsoleteBlockAllMixedContent", params);
    }
    return new nsBlockAllMixedContentDirective(directive);
  }

  // special case handling for upgrade-insecure-requests
  if (directive == nsIContentSecurityPolicy::UPGRADE_IF_INSECURE_DIRECTIVE) {
    return new nsUpgradeInsecureDirective(directive);
  }

  // if we have a child-src, cache it as a fallback for
  //   * workers (if worker-src is not explicitly specified)
  //   * frames  (if frame-src is not explicitly specified)
  if (directive == nsIContentSecurityPolicy::CHILD_SRC_DIRECTIVE) {
    mChildSrc = new nsCSPChildSrcDirective(directive);
    return mChildSrc;
  }

  // if we have a frame-src, cache it so we can discard child-src for frames
  if (directive == nsIContentSecurityPolicy::FRAME_SRC_DIRECTIVE) {
    mFrameSrc = new nsCSPDirective(directive);
    return mFrameSrc;
  }

  // if we have a worker-src, cache it so we can discard child-src for workers
  if (directive == nsIContentSecurityPolicy::WORKER_SRC_DIRECTIVE) {
    mWorkerSrc = new nsCSPDirective(directive);
    return mWorkerSrc;
  }

  // if we have a script-src, cache it as a fallback for worker-src
  // in case child-src is not present. It is also used as a fallback for
  // script-src-elem and script-src-attr.
  if (directive == nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE) {
    mScriptSrc = new nsCSPScriptSrcDirective(directive);
    return mScriptSrc;
  }

  // If we have a style-src, cache it as a fallback for style-src-elem and
  // style-src-attr.
  if (directive == nsIContentSecurityPolicy::STYLE_SRC_DIRECTIVE) {
    mStyleSrc = new nsCSPStyleSrcDirective(directive);
    return mStyleSrc;
  }

  return new nsCSPDirective(directive);
}

// directive = *WSP [ directive-name [ WSP directive-value ] ]
void nsCSPParser::directive() {
  // Make sure that the directive-srcs-array contains at least
  // one directive.
  if (mCurDir.Length() == 0) {
    AutoTArray<nsString, 1> params = {u"directive missing"_ns};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "failedToParseUnrecognizedSource", params);
    return;
  }

  // Set the directiveName to mCurToken
  // Remember, the directive name is stored at index 0
  mCurToken = mCurDir[0];

  CSPPARSERLOG(("nsCSPParser::directive, mCurToken: %s, mCurValue: %s",
                NS_ConvertUTF16toUTF8(mCurToken).get(),
                NS_ConvertUTF16toUTF8(mCurValue).get()));

  if (CSP_IsEmptyDirective(mCurValue, mCurToken)) {
    return;
  }

  // Try to create a new CSPDirective
  nsCSPDirective* cspDir = directiveName();
  if (!cspDir) {
    // if we can not create a CSPDirective, we can skip parsing the srcs for
    // that array
    return;
  }

  // special case handling for block-all-mixed-content, which is only specified
  // by a directive name but does not include any srcs.
  if (cspDir->equals(nsIContentSecurityPolicy::BLOCK_ALL_MIXED_CONTENT)) {
    if (mCurDir.Length() > 1) {
      AutoTArray<nsString, 1> params = {u"block-all-mixed-content"_ns};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "ignoreSrcForDirective", params);
    }
    // add the directive and return
    mPolicy->addDirective(cspDir);
    return;
  }

  // special case handling for upgrade-insecure-requests, which is only
  // specified by a directive name but does not include any srcs.
  if (cspDir->equals(nsIContentSecurityPolicy::UPGRADE_IF_INSECURE_DIRECTIVE)) {
    if (mCurDir.Length() > 1) {
      AutoTArray<nsString, 1> params = {u"upgrade-insecure-requests"_ns};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "ignoreSrcForDirective", params);
    }
    // add the directive and return
    mPolicy->addUpgradeInsecDir(
        static_cast<nsUpgradeInsecureDirective*>(cspDir));
    return;
  }

  // special case handling for report-uri directive (since it doesn't contain
  // a valid source list but rather actual URIs)
  if (CSP_IsDirective(mCurDir[0],
                      nsIContentSecurityPolicy::REPORT_URI_DIRECTIVE)) {
    reportURIList(cspDir);
    return;
  }

  // special case handling for report-to directive (since it doesn't contain
  // a valid source list but rather an endpoint group)
  if (CSP_IsDirective(mCurDir[0],
                      nsIContentSecurityPolicy::REPORT_TO_DIRECTIVE)) {
    reportGroup(cspDir);
    return;
  }

  // special case handling for sandbox directive (since it doe4sn't contain
  // a valid source list but rather special sandbox flags)
  if (CSP_IsDirective(mCurDir[0],
                      nsIContentSecurityPolicy::SANDBOX_DIRECTIVE)) {
    sandboxFlagList(cspDir);
    return;
  }

  // Special case handling since these directives don't contain source lists.
  if (CSP_IsDirective(
          mCurDir[0],
          nsIContentSecurityPolicy::REQUIRE_TRUSTED_TYPES_FOR_DIRECTIVE)) {
    handleRequireTrustedTypesForDirective(cspDir);
    return;
  }

  if (cspDir->equals(nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE)) {
    handleTrustedTypesDirective(cspDir);
    return;
  }

  // make sure to reset cache variables when trying to invalidate unsafe-inline;
  // unsafe-inline might not only appear in script-src, but also in default-src
  mHasHashOrNonce = false;
  mHasAnyUnsafeEval = false;
  mStrictDynamic = false;
  mUnsafeInlineKeywordSrc = nullptr;

  mParsingFrameAncestorsDir = CSP_IsDirective(
      mCurDir[0], nsIContentSecurityPolicy::FRAME_ANCESTORS_DIRECTIVE);

  // Try to parse all the srcs by handing the array off to directiveValue
  nsTArray<nsCSPBaseSrc*> srcs;
  directiveValue(srcs);

  // If we can not parse any srcs; we let the source expression be the empty set
  // ('none') see, http://www.w3.org/TR/CSP11/#source-list-parsing
  if (srcs.IsEmpty() || (srcs.Length() == 1 && srcs[0]->isReportSample())) {
    nsCSPKeywordSrc* keyword = new nsCSPKeywordSrc(CSP_NONE);
    srcs.InsertElementAt(0, keyword);
  }

  MaybeWarnAboutIgnoredSources(srcs);
  MaybeWarnAboutUnsafeInline(*cspDir);
  MaybeWarnAboutUnsafeEval(*cspDir);

  // Add the newly created srcs to the directive and add the directive to the
  // policy
  cspDir->addSrcs(srcs);
  mPolicy->addDirective(cspDir);
}

void nsCSPParser::MaybeWarnAboutIgnoredSources(
    const nsTArray<nsCSPBaseSrc*>& aSrcs) {
  // If policy contains 'strict-dynamic' warn about ignored sources.
  if (mStrictDynamic &&
      !CSP_IsDirective(mCurDir[0],
                       nsIContentSecurityPolicy::DEFAULT_SRC_DIRECTIVE)) {
    for (uint32_t i = 0; i < aSrcs.Length(); i++) {
      nsAutoString srcStr;
      aSrcs[i]->toString(srcStr);
      // Hashes and nonces continue to apply with 'strict-dynamic', as well as
      // 'unsafe-eval', 'wasm-unsafe-eval', 'trusted-types-eval' and
      // 'unsafe-hashes'.
      if (!aSrcs[i]->isKeyword(CSP_STRICT_DYNAMIC) &&
          !aSrcs[i]->isKeyword(CSP_TRUSTED_TYPES_EVAL) &&
          !aSrcs[i]->isKeyword(CSP_UNSAFE_EVAL) &&
          !aSrcs[i]->isKeyword(CSP_WASM_UNSAFE_EVAL) &&
          !aSrcs[i]->isKeyword(CSP_UNSAFE_HASHES) && !aSrcs[i]->isNonce() &&
          !aSrcs[i]->isHash()) {
        AutoTArray<nsString, 2> params = {std::move(srcStr), mCurDir[0]};
        logWarningErrorToConsole(nsIScriptError::warningFlag,
                                 "ignoringScriptSrcForStrictDynamic", params);
      }
    }

    // Log a warning that all scripts might be blocked because the policy
    // contains 'strict-dynamic' but no valid nonce or hash.
    if (!mHasHashOrNonce) {
      AutoTArray<nsString, 1> params = {mCurDir[0]};
      logWarningErrorToConsole(nsIScriptError::warningFlag,
                               "strictDynamicButNoHashOrNonce", params);
    }
  }
}

void nsCSPParser::MaybeWarnAboutUnsafeInline(const nsCSPDirective& aDirective) {
  // From https://w3c.github.io/webappsec-csp/#allow-all-inline
  // follows that when either a hash or nonce is specified, 'unsafe-inline'
  // should not apply.
  if (mHasHashOrNonce && mUnsafeInlineKeywordSrc &&
      (aDirective.isDefaultDirective() ||
       aDirective.equals(nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE) ||
       aDirective.equals(nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE) ||
       aDirective.equals(nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE) ||
       aDirective.equals(nsIContentSecurityPolicy::STYLE_SRC_DIRECTIVE) ||
       aDirective.equals(nsIContentSecurityPolicy::STYLE_SRC_ELEM_DIRECTIVE) ||
       aDirective.equals(nsIContentSecurityPolicy::STYLE_SRC_ATTR_DIRECTIVE))) {
    // Log to the console that unsafe-inline will be ignored.
    AutoTArray<nsString, 2> params = {u"'unsafe-inline'"_ns, mCurDir[0]};
    logWarningErrorToConsole(nsIScriptError::warningFlag,
                             "ignoringSrcWithinNonceOrHashDirective", params);
  }
}

void nsCSPParser::MaybeWarnAboutUnsafeEval(const nsCSPDirective& aDirective) {
  if (mHasAnyUnsafeEval &&
      (aDirective.equals(nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE) ||
       aDirective.equals(
           nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE))) {
    // Log to the console that (wasm-)unsafe-eval will be ignored.
    AutoTArray<nsString, 1> params = {mCurDir[0]};
    logWarningErrorToConsole(nsIScriptError::warningFlag, "ignoringUnsafeEval",
                             params);
  }
}

// policy = [ directive *( ";" [ directive ] ) ]
nsCSPPolicy* nsCSPParser::policy() {
  CSPPARSERLOG(("nsCSPParser::policy"));

  mPolicy = new nsCSPPolicy();
  for (uint32_t i = 0; i < mTokens.Length(); i++) {
    // https://w3c.github.io/webappsec-csp/#parse-serialized-policy
    // Step 2.2. ..., or if token is not an ASCII string, continue.
    // https://w3c.github.io/webappsec-csp/#grammardef-directive-value
    // Also, if token contains characters outside 0x21-0x7E, continue.
    //
    // Note: In the spec the token isn't split by whitespace yet.
    bool isValid = true;
    for (const auto& token : mTokens[i]) {
      if (CSP_IsInvalidDirectiveValue(token)) {
        AutoTArray<nsString, 1> params = {mTokens[i][0], token};
        logWarningErrorToConsole(nsIScriptError::warningFlag,
                                 "ignoringInvalidToken", params);
        isValid = false;
        break;
      }
    }
    if (!isValid) {
      continue;
    }

    // All input is already tokenized; set one tokenized array in the form of
    // [ name, src, src, ... ]
    // to mCurDir and call directive which processes the current directive.
    mCurDir = mTokens[i].Clone();
    directive();
  }

  if (mChildSrc) {
    if (!mFrameSrc) {
      // if frame-src is specified explicitly for that policy than child-src
      // should not restrict frames; if not, than child-src needs to restrict
      // frames.
      mChildSrc->setRestrictFrames();
    }
    if (!mWorkerSrc) {
      // if worker-src is specified explicitly for that policy than child-src
      // should not restrict workers; if not, than child-src needs to restrict
      // workers.
      mChildSrc->setRestrictWorkers();
    }
  }

  // if script-src is specified, but not worker-src and also no child-src, then
  // script-src has to govern workers.
  if (mScriptSrc && !mWorkerSrc && !mChildSrc) {
    mScriptSrc->setRestrictWorkers();
  }

  // If script-src is specified and script-src-elem is not specified, then
  // script-src has to govern script requests and script blocks.
  if (mScriptSrc && !mPolicy->hasDirective(
                        nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE)) {
    mScriptSrc->setRestrictScriptElem();
  }

  // If script-src is specified and script-src-attr is not specified, then
  // script-src has to govern script attr (event handlers).
  if (mScriptSrc && !mPolicy->hasDirective(
                        nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE)) {
    mScriptSrc->setRestrictScriptAttr();
  }

  // If style-src is specified and style-src-elem is not specified, then
  // style-src serves as a fallback.
  if (mStyleSrc && !mPolicy->hasDirective(
                       nsIContentSecurityPolicy::STYLE_SRC_ELEM_DIRECTIVE)) {
    mStyleSrc->setRestrictStyleElem();
  }

  // If style-src is specified and style-attr-elem is not specified, then
  // style-src serves as a fallback.
  if (mStyleSrc && !mPolicy->hasDirective(
                       nsIContentSecurityPolicy::STYLE_SRC_ATTR_DIRECTIVE)) {
    mStyleSrc->setRestrictStyleAttr();
  }

  return mPolicy;
}

nsCSPPolicy* nsCSPParser::parseContentSecurityPolicy(
    const nsAString& aPolicyString, nsIURI* aSelfURI, bool aReportOnly,
    nsCSPContext* aCSPContext, bool aDeliveredViaMetaTag,
    bool aSuppressLogMessages) {
  if (CSPPARSERLOGENABLED()) {
    CSPPARSERLOG(("nsCSPParser::parseContentSecurityPolicy, policy: %s",
                  NS_ConvertUTF16toUTF8(aPolicyString).get()));
    CSPPARSERLOG(("nsCSPParser::parseContentSecurityPolicy, selfURI: %s",
                  aSelfURI->GetSpecOrDefault().get()));
    CSPPARSERLOG(("nsCSPParser::parseContentSecurityPolicy, reportOnly: %s",
                  (aReportOnly ? "true" : "false")));
    CSPPARSERLOG(
        ("nsCSPParser::parseContentSecurityPolicy, deliveredViaMetaTag: %s",
         (aDeliveredViaMetaTag ? "true" : "false")));
  }

  NS_ASSERTION(aSelfURI, "Can not parseContentSecurityPolicy without aSelfURI");

  // Separate all input into tokens and store them in the form of:
  // [ [ name, src, src, ... ], [ name, src, src, ... ], ... ]
  // The tokenizer itself can not fail; all eventual errors
  // are detected in the parser itself.

  nsTArray<CopyableTArray<nsString> > tokens;
  PolicyTokenizer::tokenizePolicy(aPolicyString, tokens);

  nsCSPParser parser(tokens, aSelfURI, aCSPContext, aDeliveredViaMetaTag,
                     aSuppressLogMessages);

  // Start the parser to generate a new CSPPolicy using the generated tokens.
  nsCSPPolicy* policy = parser.policy();

  // Check that report-only policies define a report-uri, otherwise log warning.
  if (aReportOnly) {
    policy->setReportOnlyFlag(true);
    if (!policy->hasDirective(nsIContentSecurityPolicy::REPORT_TO_DIRECTIVE) &&
        !policy->hasDirective(nsIContentSecurityPolicy::REPORT_URI_DIRECTIVE) &&
        !CSP_IsBrowserXHTML(aSelfURI)) {
      nsAutoCString prePath;
      nsresult rv = aSelfURI->GetPrePath(prePath);
      NS_ENSURE_SUCCESS(rv, policy);
      AutoTArray<nsString, 1> params;
      CopyUTF8toUTF16(prePath, *params.AppendElement());
      parser.logWarningErrorToConsole(
          nsIScriptError::warningFlag,
          "reportURINorReportToNotInReportOnlyHeader", params);
    }
  }

  policy->setDeliveredViaMetaTagFlag(aDeliveredViaMetaTag);

  if (policy->getNumDirectives() == 0) {
    // Individual errors were already reported in the parser, but if
    // we do not have an enforcable directive at all, we return null.
    delete policy;
    return nullptr;
  }

  if (CSPPARSERLOGENABLED()) {
    nsString parsedPolicy;
    policy->toString(parsedPolicy);
    CSPPARSERLOG(("nsCSPParser::parseContentSecurityPolicy, parsedPolicy: %s",
                  NS_ConvertUTF16toUTF8(parsedPolicy).get()));
  }

  return policy;
}
