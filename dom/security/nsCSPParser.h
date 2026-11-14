/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCSPParser_h_
#define nsCSPParser_h_

#include "PolicyTokenizer.h"
#include "nsCSPContext.h"
#include "nsCSPUtils.h"
#include "nsIURI.h"

bool isNumberToken(char16_t aSymbol);
bool isValidHexDig(char16_t aHexDig);

// clang-format off
const char16_t COLON        = ':';
const char16_t SEMICOLON    = ';';
const char16_t SLASH        = '/';
const char16_t PLUS         = '+';
const char16_t DASH         = '-';
const char16_t DOT          = '.';
const char16_t UNDERLINE    = '_';
const char16_t TILDE        = '~';
const char16_t WILDCARD     = '*';
const char16_t SINGLEQUOTE  = '\'';
const char16_t NUMBER_SIGN  = '#';
const char16_t QUESTIONMARK = '?';
const char16_t PERCENT_SIGN = '%';
const char16_t EXCLAMATION  = '!';
const char16_t DOLLAR       = '$';
const char16_t AMPERSAND    = '&';
const char16_t OPENBRACE    = '(';
const char16_t CLOSINGBRACE = ')';
const char16_t EQUALS       = '=';
const char16_t ATSYMBOL     = '@';
// clang-format on

class nsCSPParser {
 public:
  /**
   * The CSP parser only has one main publicly accessible function, which is
   * parseContentSecurityPolicy. Internally the input string is separated into
   * string tokens and policy() is called, which starts parsing the policy. The
   * parser calls one function after the other according the the source-list
   * from http://www.w3.org/TR/CSP11/#source-list. E.g., the parser can only
   * call port() after the parser has already processed any possible host in
   * host(), similar to a finite state machine.
   */
  static nsCSPPolicy* parseContentSecurityPolicy(const nsAString& aPolicyString,
                                                 nsIURI* aSelfURI,
                                                 bool aReportOnly,
                                                 nsCSPContext* aCSPContext,
                                                 bool aDeliveredViaMetaTag,
                                                 bool aSuppressLogMessages);

  /**
   * Check if given string is valid base64 as per CSP spec.
   * https://w3c.github.io/webappsec-csp/#grammardef-base64-value
   */
  static bool isValidBase64Value(const nsAString& aValue);

 private:
  nsCSPParser(policyTokens& aTokens, nsIURI* aSelfURI,
              nsCSPContext* aCSPContext, bool aDeliveredViaMetaTag,
              bool aSuppressLogMessages);

  ~nsCSPParser();

  // Parsing the CSP using the source-list from
  // http://www.w3.org/TR/CSP11/#source-list
  nsCSPPolicy* policy();
  void directive();
  nsCSPDirective* directiveName();
  void directiveValue(nsTArray<nsCSPBaseSrc*>& outSrcs);
  void referrerDirectiveValue(nsCSPDirective* aDir);
  void reportURIList(nsCSPDirective* aDir);
  void reportGroup(nsCSPDirective* aDir);
  void sandboxFlagList(nsCSPDirective* aDir);
  void handleRequireTrustedTypesForDirective(nsCSPDirective* aDir);
  void handleTrustedTypesDirective(nsCSPDirective* aDir);
  void sourceList(nsTArray<nsCSPBaseSrc*>& outSrcs);
  nsCSPBaseSrc* sourceExpression();
  nsCSPSchemeSrc* schemeSource();
  nsCSPHostSrc* hostSource();
  nsCSPBaseSrc* keywordSource();
  nsCSPNonceSrc* nonceSource();
  nsCSPHashSrc* hashSource();
  nsCSPUrlHashSrc* urlHashSource();
  nsCSPHostSrc* host();
  bool hostChar();
  bool schemeChar();
  bool port();
  bool path(nsCSPHostSrc* aCspHost);

  bool subHost();                        // helper function to parse subDomains
  bool atValidUnreservedChar();          // helper function to parse unreserved
  bool atValidSubDelimChar();            // helper function to parse sub-delims
  bool atValidPctEncodedChar();          // helper function to parse pct-encoded
  bool subPath(nsCSPHostSrc* aCspHost);  // helper function to parse paths

  inline bool atEnd() { return mCurChar >= mEndChar; }

  inline bool accept(char16_t aSymbol) {
    if (atEnd()) {
      return false;
    }
    return (*mCurChar == aSymbol) && advance();
  }

  inline bool accept(bool (*aClassifier)(char16_t)) {
    if (atEnd()) {
      return false;
    }
    return (aClassifier(*mCurChar)) && advance();
  }

  inline bool peek(char16_t aSymbol) {
    if (atEnd()) {
      return false;
    }
    return *mCurChar == aSymbol;
  }

  inline bool peek(bool (*aClassifier)(char16_t)) {
    if (atEnd()) {
      return false;
    }
    return aClassifier(*mCurChar);
  }

  inline bool advance() {
    if (atEnd()) {
      return false;
    }
    mCurValue.Append(*mCurChar++);
    return true;
  }

  inline void resetCurValue() { mCurValue.Truncate(); }

  bool atEndOfPath();
  bool atValidPathChar();

  void resetCurChar(const nsAString& aToken);

  void logWarningErrorToConsole(uint32_t aSeverityFlag, const char* aProperty,
                                const nsTArray<nsString>& aParams);

  void logWarningForIgnoringNoneKeywordToConsole();

  void MaybeWarnAboutIgnoredSources(const nsTArray<nsCSPBaseSrc*>& aSrcs);
  void MaybeWarnAboutUnsafeInline(const nsCSPDirective& aDirective);
  void MaybeWarnAboutUnsafeEval(const nsCSPDirective& aDirective);

  /**
   * When parsing the policy, the parser internally uses the following helper
   * variables/members which are used/reset during parsing. The following
   * example explains how they are used.
   * The tokenizer separats all input into arrays of arrays of strings, which
   * are stored in mTokens, for example:
   *   mTokens = [ [ script-src, http://www.example.com, 'self' ], ... ]
   *
   * When parsing starts, mCurdir always holds the currently processed array of
   * strings.
   * In our example:
   *   mCurDir = [ script-src, http://www.example.com, 'self' ]
   *
   * During parsing, we process/consume one string at a time of that array.
   * We set mCurToken to the string we are currently processing; in the first
   * case that would be: mCurToken = script-src which allows to do simple string
   * comparisons to see if mCurToken is a valid directive.
   *
   * Continuing parsing, the parser consumes the next string of that array,
   * resetting:
   *   mCurToken = "http://www.example.com"
   *                ^                     ^
   *                mCurChar              mEndChar (points *after* the 'm')
   *   mCurValue = ""
   *
   * After calling advance() the first time, helpers would hold the following
   * values:
   *   mCurToken = "http://www.example.com"
   *                 ^                    ^
   *                 mCurChar             mEndChar (points *after* the 'm')
   *  mCurValue = "h"
   *
   * We continue parsing till all strings of one directive are consumed, then we
   * reset mCurDir to hold the next array of strings and start the process all
   * over.
   */

  const char16_t* mCurChar;
  const char16_t* mEndChar;
  nsString mCurValue;
  nsString mCurToken;
  nsTArray<nsString> mCurDir;

  // helpers to allow invalidation of srcs within script-src and style-src
  // if either 'strict-dynamic' or at least a hash or nonce is present.
  bool mHasHashOrNonce;    // false, if no hash or nonce is defined
  bool mHasUrlHash;        // false, if no url-hash-source is defined
  bool mHasAnyUnsafeEval;  // false, if no (wasm-)unsafe-eval keyword is used.
  bool mStrictDynamic;     // false, if 'strict-dynamic' is not defined
  nsCSPKeywordSrc* mUnsafeInlineKeywordSrc;  // null, otherwise invlidate()

  // cache variables for child-src, frame-src and worker-src handling;
  // in CSP 3 child-src is deprecated. For backwards compatibility
  // child-src needs to restrict:
  //   (*) frames, in case frame-src is not expicitly specified
  //   (*) workers, in case worker-src is not expicitly specified
  // If neither worker-src, nor child-src is present, then script-src
  // needs to govern workers.
  nsCSPChildSrcDirective* mChildSrc;
  nsCSPDirective* mFrameSrc;
  nsCSPDirective* mWorkerSrc;
  nsCSPScriptSrcDirective* mScriptSrc;
  nsCSPStyleSrcDirective* mStyleSrc;

  // cache variable to let nsCSPHostSrc know that it's within
  // the frame-ancestors directive.
  bool mParsingFrameAncestorsDir;

  policyTokens mTokens;
  nsIURI* mSelfURI;
  nsCSPPolicy* mPolicy;
  nsCSPContext* mCSPContext;  // used for console logging
  bool mDeliveredViaMetaTag;
  bool mSuppressLogMessages;
};

#endif /* nsCSPParser_h_ */
