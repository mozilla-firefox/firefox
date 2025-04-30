/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>
#include "prprf.h"
#include "prmem.h"
#include "plbase64.h"
#include "nsCRT.h"
#include "nsTArray.h"
#include "nsEscape.h"
#include "nsMIMEHeaderParamImpl.h"
#include "nsNativeCharsetUtils.h"
#include "mozilla/Encoding.h"
#include "mozilla/TextUtils.h"
#include "mozilla/Utf8.h"

using mozilla::Encoding;
using mozilla::IsAscii;
using mozilla::IsUtf8;

// static functions declared below are moved from mailnews/mime/src/comi18n.cpp

static char* DecodeQ(const char*, uint32_t);
static bool Is7bitNonAsciiString(const char*, uint32_t);
static void CopyRawHeader(const char*, uint32_t, const nsACString&,
                          nsACString&);
static nsresult DecodeRFC2047Str(const char*, const nsACString&, bool,
                                 nsACString&);
static nsresult internalDecodeParameter(const nsACString&, const nsACString&,
                                        const nsACString&, bool, bool,
                                        nsACString&);

static nsresult ToUTF8(const nsACString& aString, const nsACString& aCharset,
                       bool aAllowSubstitution, nsACString& aResult) {
  if (aCharset.IsEmpty()) {
    return NS_ERROR_INVALID_ARG;
  }

  const auto* encoding = Encoding::ForLabelNoReplacement(aCharset);
  if (!encoding) {
    return NS_ERROR_UCONV_NOCONV;
  }
  if (aAllowSubstitution) {
    nsresult rv = encoding->DecodeWithoutBOMHandling(aString, aResult);
    if (NS_SUCCEEDED(rv)) {
      return NS_OK;
    }
    return rv;
  }
  return encoding->DecodeWithoutBOMHandlingAndWithoutReplacement(aString,
                                                                 aResult);
}

static nsresult ConvertStringToUTF8(const nsACString& aString,
                                    const nsACString& aCharset, bool aSkipCheck,
                                    bool aAllowSubstitution,
                                    nsACString& aUTF8String) {
  // return if ASCII only or valid UTF-8 providing that the ASCII/UTF-8
  // check is requested. It may not be asked for if a caller suspects
  // that the input is in non-ASCII 7bit charset (ISO-2022-xx, HZ) or
  // it's in a charset other than UTF-8 that can be mistaken for UTF-8.
  if (!aSkipCheck && (IsAscii(aString) || IsUtf8(aString))) {
    aUTF8String = aString;
    return NS_OK;
  }

  aUTF8String.Truncate();

  nsresult rv = ToUTF8(aString, aCharset, aAllowSubstitution, aUTF8String);

  // additional protection for cases where check is skipped and  the input
  // is actually in UTF-8 as opposed to aCharset. (i.e. caller's hunch
  // was wrong.) We don't check ASCIIness assuming there's no charset
  // incompatible with ASCII (we don't support EBCDIC).
  if (aSkipCheck && NS_FAILED(rv) && IsUtf8(aString)) {
    aUTF8String = aString;
    return NS_OK;
  }

  return rv;
}

// XXX The chance of UTF-7 being used in the message header is really
// low, but in theory it's possible.
#define IS_7BIT_NON_ASCII_CHARSET(cset)          \
  (!nsCRT::strncasecmp((cset), "ISO-2022", 8) || \
   !nsCRT::strncasecmp((cset), "HZ-GB", 5) ||    \
   !nsCRT::strncasecmp((cset), "UTF-7", 5))

NS_IMPL_ISUPPORTS(nsMIMEHeaderParamImpl, nsIMIMEHeaderParam)

NS_IMETHODIMP
nsMIMEHeaderParamImpl::GetParameter(const nsACString& aHeaderVal,
                                    const char* aParamName,
                                    const nsACString& aFallbackCharset,
                                    bool aTryLocaleCharset, char** aLang,
                                    nsAString& aResult) {
  return DoGetParameter(aHeaderVal, aParamName, MIME_FIELD_ENCODING,
                        aFallbackCharset, aTryLocaleCharset, aLang, aResult);
}

NS_IMETHODIMP
nsMIMEHeaderParamImpl::GetParameterHTTP(const nsACString& aHeaderVal,
                                        const char* aParamName,
                                        const nsACString& aFallbackCharset,
                                        bool aTryLocaleCharset, char** aLang,
                                        nsAString& aResult) {
  return DoGetParameter(aHeaderVal, aParamName, HTTP_FIELD_ENCODING,
                        aFallbackCharset, aTryLocaleCharset, aLang, aResult);
}

/* static */
nsresult nsMIMEHeaderParamImpl::GetParameterHTTP(const nsACString& aHeaderVal,
                                                 const char* aParamName,
                                                 nsAString& aResult) {
  return DoGetParameter(aHeaderVal, aParamName, HTTP_FIELD_ENCODING, ""_ns,
                        false, nullptr, aResult);
}

/* static */
// detects any non-null characters pass null
bool nsMIMEHeaderParamImpl::ContainsTrailingCharPastNull(
    const nsACString& aVal) {
  nsACString::const_iterator first;
  aVal.BeginReading(first);
  nsACString::const_iterator end;
  aVal.EndReading(end);

  if (FindCharInReadable(L'\0', first, end)) {
    while (first != end) {
      if (*first != '\0') {
        // contains trailing characters past the null character
        return true;
      }
      ++first;
    }
  }
  return false;
}

// XXX : aTryLocaleCharset is not yet effective.
/* static */
nsresult nsMIMEHeaderParamImpl::DoGetParameter(
    const nsACString& aHeaderVal, const char* aParamName,
    ParamDecoding aDecoding, const nsACString& aFallbackCharset,
    bool aTryLocaleCharset, char** aLang, nsAString& aResult) {
  aResult.Truncate();
  nsresult rv;

  // get parameter (decode RFC 2231/5987 when applicable, as specified by
  // aDecoding (5987 being a subset of 2231) and return charset.)
  nsCString med;
  nsCString charset;
  rv = DoParameterInternal(aHeaderVal, aParamName, aDecoding,
                           getter_Copies(charset), aLang, getter_Copies(med));
  if (NS_FAILED(rv)) return rv;

  // convert to UTF-8 after charset conversion and RFC 2047 decoding
  // if necessary.

  nsAutoCString str1;
  rv = internalDecodeParameter(med, charset, ""_ns, false,
                               // was aDecoding == MIME_FIELD_ENCODING
                               // see bug 875615
                               true, str1);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aFallbackCharset.IsEmpty()) {
    const Encoding* encoding = Encoding::ForLabel(aFallbackCharset);
    nsAutoCString str2;
    if (NS_SUCCEEDED(ConvertStringToUTF8(str1, aFallbackCharset, false,
                                         encoding != UTF_8_ENCODING, str2))) {
      CopyUTF8toUTF16(str2, aResult);
      return NS_OK;
    }
  }

  if (IsUtf8(str1)) {
    CopyUTF8toUTF16(str1, aResult);
    return NS_OK;
  }

  if (aTryLocaleCharset && !NS_IsNativeUTF8()) {
    return NS_CopyNativeToUnicode(str1, aResult);
  }

  CopyASCIItoUTF16(str1, aResult);
  return NS_OK;
}

// remove backslash-encoded sequences from quoted-strings
// modifies string in place, potentially shortening it
void RemoveQuotedStringEscapes(char* src) {
  char* dst = src;

  for (char* c = src; *c; ++c) {
    if (c[0] == '\\' && c[1]) {
      // skip backslash if not at end
      ++c;
    }
    *dst++ = *c;
  }
  *dst = 0;
}

// true is character is a hex digit
bool IsHexDigit(char aChar) {
  char c = aChar;

  return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
         (c >= '0' && c <= '9');
}

// validate that a C String containing %-escapes is syntactically valid
bool IsValidPercentEscaped(const char* aValue, int32_t len) {
  for (int32_t i = 0; i < len; i++) {
    if (aValue[i] == '%') {
      if (!IsHexDigit(aValue[i + 1]) || !IsHexDigit(aValue[i + 2])) {
        return false;
      }
    }
  }
  return true;
}

// Support for continuations (RFC 2231, Section 3)

// only a sane number supported
#define MAX_CONTINUATIONS 999

// part of a continuation

class Continuation {
 public:
  Continuation(const char* aValue, uint32_t aLength, bool aNeedsPercentDecoding,
               bool aWasQuotedString) {
    value = aValue;
    length = aLength;
    needsPercentDecoding = aNeedsPercentDecoding;
    wasQuotedString = aWasQuotedString;
  }
  Continuation() {
    // empty constructor needed for nsTArray
    value = nullptr;
    length = 0;
    needsPercentDecoding = false;
    wasQuotedString = false;
  }
  ~Continuation() = default;

  const char* value;
  uint32_t length;
  bool needsPercentDecoding;
  bool wasQuotedString;
};

// combine segments into a single string, returning the allocated string
// (or nullptr) while emptying the list
char* combineContinuations(nsTArray<Continuation>& aArray) {
  // Sanity check
  if (aArray.Length() == 0) return nullptr;

  // Get an upper bound for the length
  uint32_t length = 0;
  for (uint32_t i = 0; i < aArray.Length(); i++) {
    length += aArray[i].length;
  }

  // Allocate
  char* result = (char*)moz_xmalloc(length + 1);

  // Concatenate
  *result = '\0';

  for (uint32_t i = 0; i < aArray.Length(); i++) {
    Continuation cont = aArray[i];
    if (!cont.value) break;

    char* c = result + strlen(result);
    strncat(result, cont.value, cont.length);
    if (cont.needsPercentDecoding) {
      nsUnescape(c);
    }
    if (cont.wasQuotedString) {
      RemoveQuotedStringEscapes(c);
    }
  }

  // return null if empty value
  if (*result == '\0') {
    free(result);
    result = nullptr;
  }

  return result;
}

// add a continuation, return false on error if segment already has been seen
bool addContinuation(nsTArray<Continuation>& aArray, uint32_t aIndex,
                     const char* aValue, uint32_t aLength,
                     bool aNeedsPercentDecoding, bool aWasQuotedString) {
  if (aIndex < aArray.Length() && aArray[aIndex].value) {
    NS_WARNING("duplicate RC2231 continuation segment #\n");
    return false;
  }

  if (aIndex > MAX_CONTINUATIONS) {
    NS_WARNING("RC2231 continuation segment # exceeds limit\n");
    return false;
  }

  if (aNeedsPercentDecoding && aWasQuotedString) {
    NS_WARNING(
        "RC2231 continuation segment can't use percent encoding and quoted "
        "string form at the same time\n");
    return false;
  }

  Continuation cont(aValue, aLength, aNeedsPercentDecoding, aWasQuotedString);

  if (aArray.Length() <= aIndex) {
    aArray.SetLength(aIndex + 1);
  }
  aArray[aIndex] = cont;

  return true;
}

// parse a segment number; return -1 on error
int32_t parseSegmentNumber(const char* aValue, int32_t aLen) {
  if (aLen < 1) {
    NS_WARNING("segment number missing\n");
    return -1;
  }

  if (aLen > 1 && aValue[0] == '0') {
    NS_WARNING("leading '0' not allowed in segment number\n");
    return -1;
  }

  int32_t segmentNumber = 0;

  for (int32_t i = 0; i < aLen; i++) {
    if (!(aValue[i] >= '0' && aValue[i] <= '9')) {
      NS_WARNING("invalid characters in segment number\n");
      return -1;
    }

    segmentNumber *= 10;
    segmentNumber += aValue[i] - '0';
    if (segmentNumber > MAX_CONTINUATIONS) {
      NS_WARNING("Segment number exceeds sane size\n");
      return -1;
    }
  }

  return segmentNumber;
}

// validate a given octet sequence for compliance with the specified
// encoding
bool IsValidOctetSequenceForCharset(const nsACString& aCharset,
                                    const char* aOctets) {
  nsAutoCString tmpRaw;
  tmpRaw.Assign(aOctets);
  nsAutoCString tmpDecoded;

  nsresult rv = ConvertStringToUTF8(tmpRaw, aCharset, false, false, tmpDecoded);

  if (rv != NS_OK) {
    // we can't decode; charset may be unsupported, or the octet sequence
    // is broken (illegal or incomplete octet sequence contained)
    NS_WARNING(
        "RFC2231/5987 parameter value does not decode according to specified "
        "charset\n");
    return false;
  }

  return true;
}

// moved almost verbatim from mimehdrs.cpp
// char *
// MimeHeaders_get_parameter (const char *header_value, const char *parm_name,
//                            char **charset, char **language)
//
// The format of these header lines  is
// <token> [ ';' <token> '=' <token-or-quoted-string> ]*
NS_IMETHODIMP
nsMIMEHeaderParamImpl::GetParameterInternal(const nsACString& aHeaderValue,
                                            const char* aParamName,
                                            char** aCharset, char** aLang,
                                            char** aResult) {
  return DoParameterInternal(aHeaderValue, aParamName, MIME_FIELD_ENCODING,
                             aCharset, aLang, aResult);
}

/* static */
nsresult nsMIMEHeaderParamImpl::DoParameterInternal(
    const nsACString& aHeaderValue, const char* aParamName,
    ParamDecoding aDecoding, char** aCharset, char** aLang, char** aResult) {
  if (aHeaderValue.IsEmpty() || !aResult) {
    return NS_ERROR_INVALID_ARG;
  }

  if (ContainsTrailingCharPastNull(aHeaderValue)) {
    // See Bug 1784348
    return NS_ERROR_INVALID_ARG;
  }

  const nsCString& flat = PromiseFlatCString(aHeaderValue);
  const char* str = flat.get();

  if (!*str) {
    return NS_ERROR_INVALID_ARG;
  }

  *aResult = nullptr;

  if (aCharset) *aCharset = nullptr;
  if (aLang) *aLang = nullptr;

  nsAutoCString charset;

  // change to (aDecoding != HTTP_FIELD_ENCODING) when we want to disable
  // them for HTTP header fields later on, see bug 776324
  bool acceptContinuations = true;

  // skip leading white space.
  for (; *str && nsCRT::IsAsciiSpace(*str); ++str) {
    ;
  }
  const char* start = str;

  // aParamName is empty. return the first (possibly) _unnamed_ 'parameter'
  // For instance, return 'inline' in the following case:
  // Content-Disposition: inline; filename=.....
  if (!aParamName || !*aParamName) {
    for (; *str && *str != ';' && !nsCRT::IsAsciiSpace(*str); ++str) {
      ;
    }
    if (str == start) return NS_ERROR_FIRST_HEADER_FIELD_COMPONENT_EMPTY;

    *aResult = (char*)moz_xmemdup(start, (str - start) + 1);
    (*aResult)[str - start] = '\0';  // null-terminate
    return NS_OK;
  }

  /* Skip forward to first ';' */
  for (; *str && *str != ';' && *str != ','; ++str) {
    ;
  }
  if (*str) str++;
  /* Skip over following whitespace */
  for (; *str && nsCRT::IsAsciiSpace(*str); ++str) {
    ;
  }

  // Some broken http servers just specify parameters
  // like 'filename' without specifying disposition
  // method. Rewind to the first non-white-space
  // character.

  if (!*str) str = start;

  // RFC2231 - The legitimate parm format can be:
  // A. title=ThisIsTitle
  // B. title*=us-ascii'en-us'This%20is%20wierd.
  // C. title*0*=us-ascii'en'This%20is%20wierd.%20We
  //    title*1*=have%20to%20support%20this.
  //    title*2="Else..."
  // D. title*0="Hey, what you think you are doing?"
  //    title*1="There is no charset and lang info."
  // RFC5987: only A and B

  // collect results for the different algorithms (plain filename,
  // RFC5987/2231-encoded filename, + continuations) separately and decide
  // which to use at the end
  char* caseAResult = nullptr;
  char* caseBResult = nullptr;
  char* caseCDResult = nullptr;

  // collect continuation segments
  nsTArray<Continuation> segments;

  // our copies of the charset parameter, kept separately as they might
  // differ for the two formats
  nsDependentCSubstring charsetB, charsetCD;

  nsDependentCSubstring lang;

  int32_t paramLen = strlen(aParamName);

  while (*str) {
    // find name/value

    const char* nameStart = str;
    const char* nameEnd = nullptr;
    const char* valueStart = nullptr;
    const char* valueEnd = nullptr;
    bool isQuotedString = false;

    NS_ASSERTION(!nsCRT::IsAsciiSpace(*str), "should be after whitespace.");

    // Skip forward to the end of this token.
    for (; *str && !nsCRT::IsAsciiSpace(*str) && *str != '=' && *str != ';';
         str++) {
      ;
    }
    nameEnd = str;

    int32_t nameLen = nameEnd - nameStart;

    // Skip over whitespace, '=', and whitespace
    while (nsCRT::IsAsciiSpace(*str)) ++str;
    if (!*str) {
      break;
    }
    if (*str != '=') {
      // don't accept parameters without "="
      goto increment_str;
    }
    // Skip over '=' only if it was actually there
    str++;
    while (nsCRT::IsAsciiSpace(*str)) ++str;

    if (*str != '"') {
      // The value is a token, not a quoted string.
      valueStart = str;
      for (valueEnd = str; *valueEnd && *valueEnd != ';'; valueEnd++) {
        ;
      }
      // ignore trailing whitespace:
      while (valueEnd > valueStart && nsCRT::IsAsciiSpace(*(valueEnd - 1))) {
        valueEnd--;
      }
      str = valueEnd;
    } else {
      isQuotedString = true;

      ++str;
      valueStart = str;
      for (valueEnd = str; *valueEnd; ++valueEnd) {
        if (*valueEnd == '\\' && *(valueEnd + 1)) {
          ++valueEnd;
        } else if (*valueEnd == '"') {
          break;
        }
      }
      str = valueEnd;
      // *valueEnd != null means that *valueEnd is quote character.
      if (*valueEnd) str++;
    }

    // See if this is the simplest case (case A above),
    // a 'single' line value with no charset and lang.
    // If so, copy it and return.
    if (nameLen == paramLen &&
        !nsCRT::strncasecmp(nameStart, aParamName, paramLen)) {
      if (caseAResult) {
        // we already have one caseA result, ignore subsequent ones
        goto increment_str;
      }

      // if the parameter spans across multiple lines we have to strip out the
      //     line continuation -- jht 4/29/98
      nsAutoCString tempStr(valueStart, valueEnd - valueStart);
      tempStr.StripCRLF();
      char* res = ToNewCString(tempStr, mozilla::fallible);
      NS_ENSURE_TRUE(res, NS_ERROR_OUT_OF_MEMORY);

      if (isQuotedString) RemoveQuotedStringEscapes(res);

      caseAResult = res;
      // keep going, we may find a RFC 2231/5987 encoded alternative
    }
    // case B, C, and D
    else if (nameLen > paramLen &&
             !nsCRT::strncasecmp(nameStart, aParamName, paramLen) &&
             *(nameStart + paramLen) == '*') {
      // 1st char past '*'
      const char* cp = nameStart + paramLen + 1;

      // if param name ends in "*" we need do to RFC5987 "ext-value" decoding
      bool needExtDecoding = *(nameEnd - 1) == '*';

      bool caseB = nameLen == paramLen + 1;
      bool caseCStart = (*cp == '0') && needExtDecoding;

      // parse the segment number
      int32_t segmentNumber = -1;
      if (!caseB) {
        int32_t segLen = (nameEnd - cp) - (needExtDecoding ? 1 : 0);
        segmentNumber = parseSegmentNumber(cp, segLen);

        if (segmentNumber == -1) {
          acceptContinuations = false;
          goto increment_str;
        }
      }

      // CaseB and start of CaseC: requires charset and optional language
      // in quotes (quotes required even if lang is blank)
      if (caseB || (caseCStart && acceptContinuations)) {
        // look for single quotation mark(')
        const char* sQuote1 = strchr(valueStart, 0x27);
        const char* sQuote2 = sQuote1 ? strchr(sQuote1 + 1, 0x27) : nullptr;

        // Two single quotation marks must be present even in
        // absence of charset and lang.
        if (!sQuote1 || !sQuote2) {
          NS_WARNING(
              "Mandatory two single quotes are missing in header parameter\n");
        }

        const char* charsetStart = nullptr;
        int32_t charsetLength = 0;
        const char* langStart = nullptr;
        int32_t langLength = 0;
        const char* rawValStart = nullptr;
        int32_t rawValLength = 0;

        if (sQuote2 && sQuote1) {
          // both delimiters present: charSet'lang'rawVal
          rawValStart = sQuote2 + 1;
          rawValLength = valueEnd - rawValStart;

          langStart = sQuote1 + 1;
          langLength = sQuote2 - langStart;

          charsetStart = valueStart;
          charsetLength = sQuote1 - charsetStart;
        } else if (sQuote1) {
          // one delimiter; assume charset'rawVal
          rawValStart = sQuote1 + 1;
          rawValLength = valueEnd - rawValStart;

          charsetStart = valueStart;
          charsetLength = sQuote1 - valueStart;
        } else {
          // no delimiter: just rawVal
          rawValStart = valueStart;
          rawValLength = valueEnd - valueStart;
        }

        if (langLength != 0) {
          lang.Assign(langStart, langLength);
        }

        // keep the charset for later
        if (caseB) {
          charsetB.Assign(charsetStart, charsetLength);
        } else {
          // if caseCorD
          charsetCD.Assign(charsetStart, charsetLength);
        }

        // non-empty value part
        if (rawValLength > 0) {
          if (!caseBResult && caseB) {
            if (!IsValidPercentEscaped(rawValStart, rawValLength)) {
              goto increment_str;
            }

            // allocate buffer for the raw value
            char* tmpResult = (char*)moz_xmemdup(rawValStart, rawValLength + 1);
            *(tmpResult + rawValLength) = 0;

            nsUnescape(tmpResult);
            caseBResult = tmpResult;
          } else {
            // caseC
            bool added = addContinuation(segments, 0, rawValStart, rawValLength,
                                         needExtDecoding, isQuotedString);

            if (!added) {
              // continuation not added, stop processing them
              acceptContinuations = false;
            }
          }
        }
      }  // end of if-block :  title*0*=  or  title*=
      // caseD: a line of multiline param with no need for unescaping :
      // title*[0-9]= or 2nd or later lines of a caseC param : title*[1-9]*=
      else if (acceptContinuations && segmentNumber != -1) {
        uint32_t valueLength = valueEnd - valueStart;

        bool added =
            addContinuation(segments, segmentNumber, valueStart, valueLength,
                            needExtDecoding, isQuotedString);

        if (!added) {
          // continuation not added, stop processing them
          acceptContinuations = false;
        }
      }  // end of if-block :  title*[0-9]= or title*[1-9]*=
    }

    // str now points after the end of the value.
    //   skip over whitespace, ';', whitespace.
  increment_str:
    while (nsCRT::IsAsciiSpace(*str)) ++str;
    if (*str == ';') {
      ++str;
    } else {
      // stop processing the header field; either we are done or the
      // separator was missing
      break;
    }
    while (nsCRT::IsAsciiSpace(*str)) ++str;
  }

  caseCDResult = combineContinuations(segments);

  if (caseBResult && !charsetB.IsEmpty()) {
    // check that the 2231/5987 result decodes properly given the
    // specified character set
    if (!IsValidOctetSequenceForCharset(charsetB, caseBResult)) {
      free(caseBResult);
      caseBResult = nullptr;
    }
  }

  if (caseCDResult && !charsetCD.IsEmpty()) {
    // check that the 2231/5987 result decodes properly given the
    // specified character set
    if (!IsValidOctetSequenceForCharset(charsetCD, caseCDResult)) {
      free(caseCDResult);
      caseCDResult = nullptr;
    }
  }

  if (caseBResult) {
    // prefer simple 5987 format over 2231 with continuations
    *aResult = caseBResult;
    caseBResult = nullptr;
    charset.Assign(charsetB);
  } else if (caseCDResult) {
    // prefer 2231/5987 with or without continuations over plain format
    *aResult = caseCDResult;
    caseCDResult = nullptr;
    charset.Assign(charsetCD);
  } else if (caseAResult) {
    *aResult = caseAResult;
    caseAResult = nullptr;
  }

  // free unused stuff
  free(caseAResult);
  free(caseBResult);
  free(caseCDResult);

  // if we have a result
  if (*aResult) {
    // then return charset and lang as well
    if (aLang && !lang.IsEmpty()) {
      uint32_t len = lang.Length();
      *aLang = (char*)moz_xmemdup(lang.BeginReading(), len + 1);
      *(*aLang + len) = 0;
    }
    if (aCharset && !charset.IsEmpty()) {
      uint32_t len = charset.Length();
      *aCharset = (char*)moz_xmemdup(charset.BeginReading(), len + 1);
      *(*aCharset + len) = 0;
    }
  }

  return *aResult ? NS_OK : NS_ERROR_INVALID_ARG;
}

nsresult internalDecodeRFC2047Header(const char* aHeaderVal,
                                     const nsACString& aDefaultCharset,
                                     bool aOverrideCharset,
                                     bool aEatContinuations,
                                     nsACString& aResult) {
  aResult.Truncate();
  if (!aHeaderVal) return NS_ERROR_INVALID_ARG;
  if (!*aHeaderVal) return NS_OK;

  // If aHeaderVal is RFC 2047 encoded or is not a UTF-8 string  but
  // aDefaultCharset is specified, decodes RFC 2047 encoding and converts
  // to UTF-8. Otherwise, just strips away CRLF.
  if (strstr(aHeaderVal, "=?") ||
      (!aDefaultCharset.IsEmpty() &&
       (!IsUtf8(nsDependentCString(aHeaderVal)) ||
        Is7bitNonAsciiString(aHeaderVal, strlen(aHeaderVal))))) {
    DecodeRFC2047Str(aHeaderVal, aDefaultCharset, aOverrideCharset, aResult);
  } else if (aEatContinuations &&
             (strchr(aHeaderVal, '\n') || strchr(aHeaderVal, '\r'))) {
    aResult = aHeaderVal;
  } else {
    aEatContinuations = false;
    aResult = aHeaderVal;
  }

  if (aEatContinuations) {
    nsAutoCString temp(aResult);
    temp.ReplaceSubstring("\n\t", " ");
    temp.ReplaceSubstring("\r\t", " ");
    temp.StripCRLF();
    aResult = temp;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMIMEHeaderParamImpl::DecodeRFC2047Header(const char* aHeaderVal,
                                           const char* aDefaultCharset,
                                           bool aOverrideCharset,
                                           bool aEatContinuations,
                                           nsACString& aResult) {
  return internalDecodeRFC2047Header(aHeaderVal, nsCString(aDefaultCharset),
                                     aOverrideCharset, aEatContinuations,
                                     aResult);
}

// true if the character is allowed in a RFC 5987 value
// see RFC 5987, Section 3.2.1, "attr-char"
bool IsRFC5987AttrChar(char aChar) {
  char c = aChar;

  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         (c == '!' || c == '#' || c == '$' || c == '&' || c == '+' ||
          c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
          c == '|' || c == '~');
}

// percent-decode a value
// returns false on failure
bool PercentDecode(nsACString& aValue) {
  char* c = (char*)moz_xmalloc(aValue.Length() + 1);

  strcpy(c, PromiseFlatCString(aValue).get());
  nsUnescape(c);
  aValue.Assign(c);
  free(c);

  return true;
}

// Decode a parameter value using the encoding defined in RFC 5987
//
// charset  "'" [ language ] "'" value-chars
NS_IMETHODIMP
nsMIMEHeaderParamImpl::DecodeRFC5987Param(const nsACString& aParamVal,
                                          nsACString& aLang,
                                          nsAString& aResult) {
  nsAutoCString charset;
  nsAutoCString language;
  nsAutoCString value;

  uint32_t delimiters = 0;
  const nsCString& encoded = PromiseFlatCString(aParamVal);
  const char* c = encoded.get();

  while (*c) {
    char tc = *c++;

    if (tc == '\'') {
      // single quote
      delimiters++;
    } else if (((unsigned char)tc) >= 128) {
      // fail early, not ASCII
      NS_WARNING("non-US-ASCII character in RFC5987-encoded param");
      return NS_ERROR_INVALID_ARG;
    } else {
      if (delimiters == 0) {
        // valid characters are checked later implicitly
        charset.Append(tc);
      } else if (delimiters == 1) {
        // no value checking for now
        language.Append(tc);
      } else if (delimiters == 2) {
        if (IsRFC5987AttrChar(tc)) {
          value.Append(tc);
        } else if (tc == '%') {
          if (!IsHexDigit(c[0]) || !IsHexDigit(c[1])) {
            // we expect two more characters
            NS_WARNING("broken %-escape in RFC5987-encoded param");
            return NS_ERROR_INVALID_ARG;
          }
          value.Append(tc);
          // we consume two more
          value.Append(*c++);
          value.Append(*c++);
        } else {
          // character not allowed here
          NS_WARNING("invalid character in RFC5987-encoded param");
          return NS_ERROR_INVALID_ARG;
        }
      }
    }
  }

  if (delimiters != 2) {
    NS_WARNING("missing delimiters in RFC5987-encoded param");
    return NS_ERROR_INVALID_ARG;
  }

  // abort early for unsupported encodings
  if (!charset.LowerCaseEqualsLiteral("utf-8")) {
    NS_WARNING("unsupported charset in RFC5987-encoded param");
    return NS_ERROR_INVALID_ARG;
  }

  // percent-decode
  if (!PercentDecode(value)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // return the encoding
  aLang.Assign(language);

  // finally convert octet sequence to UTF-8 and be done
  nsAutoCString utf8;
  nsresult rv = ConvertStringToUTF8(value, charset, true, false, utf8);
  NS_ENSURE_SUCCESS(rv, rv);

  CopyUTF8toUTF16(utf8, aResult);
  return NS_OK;
}

nsresult internalDecodeParameter(const nsACString& aParamValue,
                                 const nsACString& aCharset,
                                 const nsACString& aDefaultCharset,
                                 bool aOverrideCharset, bool aDecode2047,
                                 nsACString& aResult) {
  aResult.Truncate();
  // If aCharset is given, aParamValue was obtained from RFC2231/5987
  // encoding and we're pretty sure that it's in aCharset.
  if (!aCharset.IsEmpty()) {
    return ConvertStringToUTF8(aParamValue, aCharset, true, true, aResult);
  }

  const nsCString& param = PromiseFlatCString(aParamValue);
  nsAutoCString unQuoted;
  nsACString::const_iterator s, e;
  param.BeginReading(s);
  param.EndReading(e);

  // strip '\' when used to quote CR, LF, '"' and '\'
  for (; s != e; ++s) {
    if ((*s == '\\')) {
      if (++s == e) {
        --s;  // '\' is at the end. move back and append '\'.
      } else if (*s != nsCRT::CR && *s != nsCRT::LF && *s != '"' &&
                 *s != '\\') {
        --s;  // '\' is not foll. by CR,LF,'"','\'. move back and append '\'
      }
      // else : skip '\' and append the quoted character.
    }
    unQuoted.Append(*s);
  }

  aResult = unQuoted;
  nsresult rv = NS_OK;

  if (aDecode2047) {
    nsAutoCString decoded;

    // Try RFC 2047 encoding, instead.
    rv = internalDecodeRFC2047Header(unQuoted.get(), aDefaultCharset,
                                     aOverrideCharset, true, decoded);

    if (NS_SUCCEEDED(rv) && !decoded.IsEmpty()) aResult = decoded;
  }

  return rv;
}

NS_IMETHODIMP
nsMIMEHeaderParamImpl::DecodeParameter(const nsACString& aParamValue,
                                       const char* aCharset,
                                       const char* aDefaultCharset,
                                       bool aOverrideCharset,
                                       nsACString& aResult) {
  return internalDecodeParameter(aParamValue, nsCString(aCharset),
                                 nsCString(aDefaultCharset), aOverrideCharset,
                                 true, aResult);
}

#define ISHEXCHAR(c)                             \
  ((0x30 <= uint8_t(c) && uint8_t(c) <= 0x39) || \
   (0x41 <= uint8_t(c) && uint8_t(c) <= 0x46) || \
   (0x61 <= uint8_t(c) && uint8_t(c) <= 0x66))

// Decode Q encoding (RFC 2047).
// static
char* DecodeQ(const char* in, uint32_t length) {
  char *out, *dest = nullptr;

  out = dest = (char*)calloc(length + 1, sizeof(char));
  if (dest == nullptr) return nullptr;
  while (length > 0) {
    unsigned c = 0;
    switch (*in) {
      case '=':
        // check if |in| in the form of '=hh'  where h is [0-9a-fA-F].
        if (length < 3 || !ISHEXCHAR(in[1]) || !ISHEXCHAR(in[2])) {
          goto badsyntax;
        }
        // Can't fail because of the test above
        mozilla::Unused << PR_sscanf(in + 1, "%2X", &c);
        *out++ = (char)c;
        in += 3;
        length -= 3;
        break;

      case '_':
        *out++ = ' ';
        in++;
        length--;
        break;

      default:
        if (*in & 0x80) goto badsyntax;
        *out++ = *in++;
        length--;
    }
  }
  *out++ = '\0';

  for (out = dest; *out; ++out) {
    if (*out == '\t') *out = ' ';
  }

  return dest;

badsyntax:
  free(dest);
  return nullptr;
}

// check if input is HZ (a 7bit encoding for simplified Chinese : RFC 1842))
// or has  ESC which may be an  indication that  it's in one of many ISO
// 2022 7bit  encodings (e.g. ISO-2022-JP(-2)/CN : see RFC 1468, 1922, 1554).
// static
bool Is7bitNonAsciiString(const char* input, uint32_t len) {
  int32_t c;

  enum {
    hz_initial,    // No HZ seen yet
    hz_escaped,    // Inside an HZ ~{ escape sequence
    hz_seen,       // Have seen at least one complete HZ sequence
    hz_notpresent  // Have seen something that is not legal HZ
  } hz_state;

  hz_state = hz_initial;
  while (len) {
    c = uint8_t(*input++);
    len--;
    if (c & 0x80) return false;
    if (c == 0x1B) return true;
    if (c == '~') {
      switch (hz_state) {
        case hz_initial:
        case hz_seen:
          if (*input == '{') {
            hz_state = hz_escaped;
          } else if (*input == '~') {
            // ~~ is the HZ encoding of ~.  Skip over second ~ as well
            hz_state = hz_seen;
            input++;
            len--;
          } else {
            hz_state = hz_notpresent;
          }
          break;

        case hz_escaped:
          if (*input == '}') hz_state = hz_seen;
          break;
        default:
          break;
      }
    }
  }
  return hz_state == hz_seen;
}

#define REPLACEMENT_CHAR "\357\277\275"  // EF BF BD (UTF-8 encoding of U+FFFD)

// copy 'raw' sequences of octets in aInput to aOutput.
// If aDefaultCharset is specified, the input is assumed to be in the
// charset and converted to UTF-8. Otherwise, a blind copy is made.
// If aDefaultCharset is specified, but the conversion to UTF-8
// is not successful, each octet is replaced by Unicode replacement
// chars. *aOutput is advanced by the number of output octets.
// static
void CopyRawHeader(const char* aInput, uint32_t aLen,
                   const nsACString& aDefaultCharset, nsACString& aOutput) {
  int32_t c;

  // If aDefaultCharset is not specified, make a blind copy.
  if (aDefaultCharset.IsEmpty()) {
    aOutput.Append(aInput, aLen);
    return;
  }

  // Copy as long as it's US-ASCII.  An ESC may indicate ISO 2022
  // A ~ may indicate it is HZ
  while (aLen && (c = uint8_t(*aInput++)) != 0x1B && c != '~' && !(c & 0x80)) {
    aOutput.Append(char(c));
    aLen--;
  }
  if (!aLen) {
    return;
  }
  aInput--;

  // skip ASCIIness/UTF8ness test if aInput is supected to be a 7bit non-ascii
  // string and aDefaultCharset is a 7bit non-ascii charset.
  bool skipCheck =
      (c == 0x1B || c == '~') &&
      IS_7BIT_NON_ASCII_CHARSET(PromiseFlatCString(aDefaultCharset).get());

  // If not UTF-8, treat as default charset
  nsAutoCString utf8Text;
  if (NS_SUCCEEDED(ConvertStringToUTF8(Substring(aInput, aInput + aLen),
                                       PromiseFlatCString(aDefaultCharset),
                                       skipCheck, true, utf8Text))) {
    aOutput.Append(utf8Text);
  } else {  // replace each octet with Unicode replacement char in UTF-8.
    for (uint32_t i = 0; i < aLen; i++) {
      c = uint8_t(*aInput++);
      if (c & 0x80) {
        aOutput.Append(REPLACEMENT_CHAR);
      } else {
        aOutput.Append(char(c));
      }
    }
  }
}

nsresult DecodeQOrBase64Str(const char* aEncoded, size_t aLen, char aQOrBase64,
                            const nsACString& aCharset, nsACString& aResult) {
  char* decodedText;
  bool b64alloc = false;
  NS_ASSERTION(aQOrBase64 == 'Q' || aQOrBase64 == 'B', "Should be 'Q' or 'B'");
  if (aQOrBase64 == 'Q') {
    decodedText = DecodeQ(aEncoded, aLen);
  } else if (aQOrBase64 == 'B') {
    decodedText = PL_Base64Decode(aEncoded, aLen, nullptr);
    b64alloc = true;
  } else {
    return NS_ERROR_INVALID_ARG;
  }

  if (!decodedText) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString utf8Text;
  // skip ASCIIness/UTF8ness test if aCharset is 7bit non-ascii charset.
  nsresult rv = ConvertStringToUTF8(
      nsDependentCString(decodedText), aCharset,
      IS_7BIT_NON_ASCII_CHARSET(PromiseFlatCString(aCharset).get()), true,
      utf8Text);
  if (b64alloc) {
    PR_Free(decodedText);
  } else {
    free(decodedText);
  }
  if (NS_FAILED(rv)) {
    return rv;
  }
  aResult.Append(utf8Text);

  return NS_OK;
}

static const char especials[] = R"(()<>@,;:\"/[]?.=)";

// |decode_mime_part2_str| taken from comi18n.c
// Decode RFC2047-encoded words in the input and convert the result to UTF-8.
// If aOverrideCharset is true, charset in RFC2047-encoded words is
// ignored and aDefaultCharset is assumed, instead. aDefaultCharset
// is also used to convert raw octets (without RFC 2047 encoding) to UTF-8.
// static
nsresult DecodeRFC2047Str(const char* aHeader,
                          const nsACString& aDefaultCharset,
                          bool aOverrideCharset, nsACString& aResult) {
  const char *p, *q = nullptr, *r;
  const char* begin;  // tracking pointer for where we are in the input buffer
  int32_t isLastEncodedWord = 0;
  const char *charsetStart, *charsetEnd;
  nsAutoCString prevCharset, curCharset;
  nsAutoCString encodedText;
  char prevEncoding = '\0', curEncoding;
  nsresult rv;

  begin = aHeader;

  // To avoid buffer realloc, if possible, set capacity in advance. No
  // matter what,  more than 3x expansion can never happen for all charsets
  // supported by Mozilla. SCSU/BCSU with the sliding window set to a
  // non-BMP block may be exceptions, but Mozilla does not support them.
  // Neither any known mail/news program use them. Even if there's, we're
  // safe because we don't use a raw *char any more.
  aResult.SetCapacity(3 * strlen(aHeader));

  while ((p = strstr(begin, "=?")) != nullptr) {
    if (isLastEncodedWord) {
      // See if it's all whitespace.
      for (q = begin; q < p; ++q) {
        if (!strchr(" \t\r\n", *q)) {
          break;
        }
      }
    }

    if (!isLastEncodedWord || q < p) {
      if (!encodedText.IsEmpty()) {
        rv = DecodeQOrBase64Str(encodedText.get(), encodedText.Length(),
                                prevEncoding, prevCharset, aResult);
        if (NS_FAILED(rv)) {
          aResult.Append(encodedText);
        }
        encodedText.Truncate();
        prevCharset.Truncate();
        prevEncoding = '\0';
      }
      // copy the part before the encoded-word
      CopyRawHeader(begin, p - begin, aDefaultCharset, aResult);
      begin = p;
    }

    p += 2;

    // Get charset info
    charsetStart = p;
    charsetEnd = nullptr;
    for (q = p; *q != '?'; q++) {
      if (*q <= ' ' || strchr(especials, *q)) {
        goto badsyntax;
      }

      // RFC 2231 section 5
      if (!charsetEnd && *q == '*') {
        charsetEnd = q;
      }
    }
    if (!charsetEnd) {
      charsetEnd = q;
    }

    q++;
    curEncoding = nsCRT::ToUpper(*q);
    if (curEncoding != 'Q' && curEncoding != 'B') goto badsyntax;

    if (q[1] != '?') goto badsyntax;

    // loop-wise, keep going until we hit "?=".  the inner check handles the
    //  nul terminator should the string terminate before we hit the right
    //  marker.  (And the r[1] will never reach beyond the end of the string
    //  because *r != '?' is true if r is the nul character.)
    for (r = q + 2; *r != '?' || r[1] != '='; r++) {
      if (*r < ' ') goto badsyntax;
    }
    if (r == q + 2) {
      // it's empty, skip
      begin = r + 2;
      isLastEncodedWord = 1;
      continue;
    }

    curCharset.Assign(charsetStart, charsetEnd - charsetStart);
    // Override charset if requested.  Never override labeled UTF-8.
    // Use default charset instead of UNKNOWN-8BIT
    if ((aOverrideCharset &&
         0 != nsCRT::strcasecmp(curCharset.get(), "UTF-8")) ||
        (!aDefaultCharset.IsEmpty() &&
         0 == nsCRT::strcasecmp(curCharset.get(), "UNKNOWN-8BIT"))) {
      curCharset = aDefaultCharset;
    }

    const char* R;
    R = r;
    if (curEncoding == 'B') {
      // bug 227290. ignore an extraneous '=' at the end.
      // (# of characters in B-encoded part has to be a multiple of 4)
      int32_t n = r - (q + 2);
      R -= (n % 4 == 1 && !strncmp(r - 3, "===", 3)) ? 1 : 0;
    }
    // Bug 493544. Don't decode the encoded text until it ends
    if (R[-1] != '=' &&
        (prevCharset.IsEmpty() ||
         (curCharset == prevCharset && curEncoding == prevEncoding))) {
      encodedText.Append(q + 2, R - (q + 2));
      prevCharset = curCharset;
      prevEncoding = curEncoding;

      begin = r + 2;
      isLastEncodedWord = 1;
      continue;
    }

    bool bDecoded;  // If the current line has been decoded.
    bDecoded = false;
    if (!encodedText.IsEmpty()) {
      if (curCharset == prevCharset && curEncoding == prevEncoding) {
        encodedText.Append(q + 2, R - (q + 2));
        bDecoded = true;
      }
      rv = DecodeQOrBase64Str(encodedText.get(), encodedText.Length(),
                              prevEncoding, prevCharset, aResult);
      if (NS_FAILED(rv)) {
        aResult.Append(encodedText);
      }
      encodedText.Truncate();
      prevCharset.Truncate();
      prevEncoding = '\0';
    }
    if (!bDecoded) {
      rv = DecodeQOrBase64Str(q + 2, R - (q + 2), curEncoding, curCharset,
                              aResult);
      if (NS_FAILED(rv)) {
        aResult.Append(encodedText);
      }
    }

    begin = r + 2;
    isLastEncodedWord = 1;
    continue;

  badsyntax:
    if (!encodedText.IsEmpty()) {
      rv = DecodeQOrBase64Str(encodedText.get(), encodedText.Length(),
                              prevEncoding, prevCharset, aResult);
      if (NS_FAILED(rv)) {
        aResult.Append(encodedText);
      }
      encodedText.Truncate();
      prevCharset.Truncate();
    }
    // copy the part before the encoded-word
    aResult.Append(begin, p - begin);
    begin = p;
    isLastEncodedWord = 0;
  }

  if (!encodedText.IsEmpty()) {
    rv = DecodeQOrBase64Str(encodedText.get(), encodedText.Length(),
                            prevEncoding, prevCharset, aResult);
    if (NS_FAILED(rv)) {
      aResult.Append(encodedText);
    }
  }

  // put the tail back
  CopyRawHeader(begin, strlen(begin), aDefaultCharset, aResult);

  nsAutoCString tempStr(aResult);
  tempStr.ReplaceChar('\t', ' ');
  aResult = tempStr;

  return NS_OK;
}
