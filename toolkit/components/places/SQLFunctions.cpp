/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/storage.h"
#include "mozilla/StaticPrefs_places.h"
#include "nsString.h"
#include "nsFaviconService.h"
#include "nsNavBookmarks.h"
#include "nsUnicharUtils.h"
#include "nsWhitespaceTokenizer.h"
#include "nsEscape.h"
#include "mozIPlacesAutoComplete.h"
#include "SQLFunctions.h"
#include "nsMathUtils.h"
#include "nsUnicodeProperties.h"
#include "nsUTF8Utils.h"
#include "nsINavHistoryService.h"
#include "nsPrintfCString.h"
#include "nsNavHistory.h"
#include "mozilla/Services.h"
#include "mozilla/Utf8.h"
#include "nsURLHelper.h"
#include "nsVariant.h"
#include "nsICryptoHash.h"

// Maximum number of chars to search through.
// MatchAutoCompleteFunction won't look for matches over this threshold.
#define MAX_CHARS_TO_SEARCH_THROUGH 255

#define SECONDS_PER_DAY 86400

using namespace mozilla::storage;

////////////////////////////////////////////////////////////////////////////////
//// Anonymous Helpers

namespace {

using const_char_iterator = nsACString::const_char_iterator;
using size_type = nsACString::size_type;
using char_type = nsACString::char_type;

/**
 * Scan forward through UTF-8 text until the next potential character that
 * could match a given codepoint when lower-cased (false positives are okay).
 * This avoids having to actually parse the UTF-8 text, which is slow.
 *
 * @param aStart
 *        An iterator pointing to the first character position considered.
 *        It will be updated by this function.
 * @param aEnd
 *        An interator pointing to past-the-end of the string.
 */
static MOZ_ALWAYS_INLINE void goToNextSearchCandidate(
    const_char_iterator& aStart, const const_char_iterator& aEnd,
    uint32_t aSearchFor) {
  // If the character we search for is ASCII, then we can scan until we find
  // it or its ASCII uppercase character, modulo the special cases
  // U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE and U+212A KELVIN SIGN
  // (which are the only non-ASCII characters that lower-case to ASCII ones).
  // Since false positives are okay, we approximate ASCII lower-casing by
  // bit-ORing with 0x20, for increased performance.
  //
  // If the character we search for is *not* ASCII, we can ignore everything
  // that is, since all ASCII characters lower-case to ASCII.
  //
  // Because of how UTF-8 uses high-order bits, this will never land us
  // in the middle of a codepoint.
  //
  // The assumptions about Unicode made here are verified in the test_casing
  // gtest.
  if (aSearchFor < 128) {
    // When searching for I or K, we pick out the first byte of the UTF-8
    // encoding of the corresponding special case character, and look for it
    // in the loop below.  For other characters we fall back to 0xff, which
    // is not a valid UTF-8 byte.
    unsigned char target = (unsigned char)(aSearchFor | 0x20);
    unsigned char special = 0xff;
    if (target == 'i' || target == 'k') {
      special = (target == 'i' ? 0xc4 : 0xe2);
    }

    while (aStart < aEnd && (unsigned char)(*aStart | 0x20) != target &&
           (unsigned char)*aStart != special) {
      aStart++;
    }
  } else {
    while (aStart < aEnd && (unsigned char)(*aStart) < 128) {
      aStart++;
    }
  }
}

/**
 * Check whether a character position is on a word boundary of a UTF-8 string
 * (rather than within a word).  We define "within word" to be any position
 * between [a-zA-Z] and [a-z] -- this lets us match CamelCase words.
 * TODO: support non-latin alphabets.
 *
 * @param aPos
 *        An iterator pointing to the character position considered.  It must
 *        *not* be the first byte of a string.
 *
 * @return true if boundary, false otherwise.
 */
static MOZ_ALWAYS_INLINE bool isOnBoundary(const_char_iterator aPos) {
  if ('a' <= *aPos && *aPos <= 'z') {
    char prev = static_cast<char>(*(aPos - 1) | 0x20);
    return !('a' <= prev && prev <= 'z');
  }
  return true;
}

/**
 * Check whether a token string matches a particular position of a source
 * string, case insensitively (or optionally, case and diacritic insensitively).
 *
 * @param aTokenStart
 *        An iterator pointing to the start of the token string.
 * @param aTokenEnd
 *        An iterator pointing past-the-end of the token string.
 * @param aSourceStart
 *        An iterator pointing to the position of source string to start
 *        matching at.
 * @param aSourceEnd
 *        An iterator pointing past-the-end of the source string.
 * @param aMatchDiacritics
 *        Whether or not the match is diacritic-sensitive.
 *
 * @return true if the string [aTokenStart, aTokenEnd) matches the start of
 *         the string [aSourceStart, aSourceEnd, false otherwise.
 */
static MOZ_ALWAYS_INLINE bool stringMatch(const_char_iterator aTokenStart,
                                          const_char_iterator aTokenEnd,
                                          const_char_iterator aSourceStart,
                                          const_char_iterator aSourceEnd,
                                          bool aMatchDiacritics) {
  const_char_iterator tokenCur = aTokenStart, sourceCur = aSourceStart;

  while (tokenCur < aTokenEnd) {
    if (sourceCur >= aSourceEnd) {
      return false;
    }

    bool error;
    if (!CaseInsensitiveUTF8CharsEqual(sourceCur, tokenCur, aSourceEnd,
                                       aTokenEnd, &sourceCur, &tokenCur, &error,
                                       aMatchDiacritics)) {
      return false;
    }
  }

  return true;
}

enum FindInStringBehavior { eFindOnBoundary, eFindAnywhere };

/**
 * Common implementation for findAnywhere and findOnBoundary.
 *
 * @param aToken
 *        The token we're searching for
 * @param aSourceString
 *        The string in which we're searching
 * @param aBehavior
 *        eFindOnBoundary if we should only consider matchines which occur on
 *        word boundaries, or eFindAnywhere if we should consider matches
 *        which appear anywhere.
 *
 * @return true if aToken was found in aSourceString, false otherwise.
 */
static bool findInString(const nsDependentCSubstring& aToken,
                         const nsACString& aSourceString,
                         FindInStringBehavior aBehavior) {
  // GetLowerUTF8Codepoint assumes that there's at least one byte in
  // the string, so don't pass an empty token here.
  MOZ_ASSERT(!aToken.IsEmpty(), "Don't search for an empty token!");

  // We cannot match anything if there is nothing to search.
  if (aSourceString.IsEmpty()) {
    return false;
  }

  const nsNavHistory* history = nsNavHistory::GetConstHistoryService();
  bool matchDiacritics = history && history->MatchDiacritics();

  const_char_iterator tokenStart(aToken.BeginReading()),
      tokenEnd(aToken.EndReading()), tokenNext,
      sourceStart(aSourceString.BeginReading()),
      sourceEnd(aSourceString.EndReading()), sourceCur(sourceStart), sourceNext;

  uint32_t tokenFirstChar =
      GetLowerUTF8Codepoint(tokenStart, tokenEnd, &tokenNext);
  if (tokenFirstChar == uint32_t(-1)) {
    return false;
  }
  if (!matchDiacritics) {
    tokenFirstChar = ToNaked(tokenFirstChar);
  }

  for (;;) {
    if (matchDiacritics) {
      // Scan forward to the next viable candidate (if any).
      goToNextSearchCandidate(sourceCur, sourceEnd, tokenFirstChar);
    }
    if (sourceCur == sourceEnd) {
      break;
    }

    // Check whether the first character in the token matches the character
    // at sourceCur.  At the same time, get a pointer to the next character
    // in the source.
    uint32_t sourceFirstChar =
        GetLowerUTF8Codepoint(sourceCur, sourceEnd, &sourceNext);
    if (sourceFirstChar == uint32_t(-1)) {
      return false;
    }
    if (!matchDiacritics) {
      sourceFirstChar = ToNaked(sourceFirstChar);
    }

    if (sourceFirstChar == tokenFirstChar &&
        (aBehavior != eFindOnBoundary || sourceCur == sourceStart ||
         isOnBoundary(sourceCur)) &&
        stringMatch(tokenNext, tokenEnd, sourceNext, sourceEnd,
                    matchDiacritics)) {
      return true;
    }

    sourceCur = sourceNext;
  }

  return false;
}

static MOZ_ALWAYS_INLINE nsDependentCString
getSharedUTF8String(mozIStorageValueArray* aValues, uint32_t aIndex) {
  uint32_t len;
  const char* str = aValues->AsSharedUTF8String(aIndex, &len);
  if (!str) {
    return nsDependentCString("", (size_t)0);
  }
  return nsDependentCString(str, len);
}

/**
 * Gets the length of the prefix in a URI spec.  "Prefix" is defined to be the
 * scheme, colon, and, if present, two slashes.
 *
 * Examples:
 *
 *   http://example.com
 *   ~~~~~~~
 *   => length == 7
 *
 *   foo:example
 *   ~~~~
 *   => length == 4
 *
 *   not a spec
 *   => length == 0
 *
 * @param  aSpec
 *         A URI spec, or a string that may be a URI spec.
 * @return The length of the prefix in the spec.  If there isn't a prefix,
 *         returns 0.
 */
static MOZ_ALWAYS_INLINE size_type getPrefixLength(const nsACString& aSpec) {
  // To keep the search bounded, look at 64 characters at most.  The longest
  // IANA schemes are ~30, so double that and round up to a nice number.
  size_type length = std::min(static_cast<size_type>(64), aSpec.Length());
  for (size_type i = 0; i < length; ++i) {
    if (aSpec[i] == static_cast<char_type>(':')) {
      // Found the ':'.  Now skip past "//", if present.
      if (i + 2 < aSpec.Length() &&
          aSpec[i + 1] == static_cast<char_type>('/') &&
          aSpec[i + 2] == static_cast<char_type>('/')) {
        i += 2;
      }
      return i + 1;
    }
  }
  return 0;
}

/**
 * Gets the index in a URI spec of the host and port substring and optionally
 * its length.
 *
 * Examples:
 *
 *   http://example.com/
 *          ~~~~~~~~~~~
 *   => index == 7, length == 11
 *
 *   http://example.com:8888/
 *          ~~~~~~~~~~~~~~~~
 *   => index == 7, length == 16
 *
 *   http://user:pass@example.com/
 *                    ~~~~~~~~~~~
 *   => index == 17, length == 11
 *
 *   foo:example
 *       ~~~~~~~
 *   => index == 4, length == 7
 *
 *   not a spec
 *   ~~~~~~~~~~
 *   => index == 0, length == 10
 *
 * @param  aSpec
 *         A URI spec, or a string that may be a URI spec.
 * @param  _hostAndPortLength
 *         The length of the host and port substring is returned through this
 *         param.  Pass null if you don't care.
 * @return The length of the host and port substring in the spec.  If aSpec
 *         doesn't look like a URI, then the entire aSpec is assumed to be a
 *         "host and port", and this returns 0, and _hostAndPortLength will be
 *         the length of aSpec.
 */
static MOZ_ALWAYS_INLINE size_type
indexOfHostAndPort(const nsACString& aSpec, size_type* _hostAndPortLength) {
  size_type index = getPrefixLength(aSpec);
  size_type i = index;
  for (; i < aSpec.Length(); ++i) {
    // RFC 3986 (URIs): The origin ("authority") is terminated by '/', '?', or
    // '#' (or the end of the URI).
    if (aSpec[i] == static_cast<char_type>('/') ||
        aSpec[i] == static_cast<char_type>('?') ||
        aSpec[i] == static_cast<char_type>('#')) {
      break;
    }
    // RFC 3986: '@' marks the end of the userinfo component.
    if (aSpec[i] == static_cast<char_type>('@')) {
      index = i + 1;
    }
  }
  if (_hostAndPortLength) {
    *_hostAndPortLength = i - index;
  }
  return index;
}

}  // End anonymous namespace

namespace mozilla::places {

////////////////////////////////////////////////////////////////////////////////
//// AutoComplete Matching Function

/* static */
nsresult MatchAutoCompleteFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<MatchAutoCompleteFunction> function = new MatchAutoCompleteFunction();

  nsresult rv = aDBConn->CreateFunction("autocomplete_match"_ns,
                                        kArgIndexLength, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/* static */
nsDependentCSubstring MatchAutoCompleteFunction::fixupURISpec(
    const nsACString& aURISpec, int32_t aMatchBehavior, nsACString& aSpecBuf) {
  nsDependentCSubstring fixedSpec;

  // Try to unescape the string.  If that succeeds and yields a different
  // string which is also valid UTF-8, we'll use it.
  // Otherwise, we will simply use our original string.
  bool unescaped =
      NS_UnescapeURL(aURISpec.BeginReading(), (int32_t)aURISpec.Length(),
                     esc_SkipControl, aSpecBuf);
  if (unescaped && IsUtf8(aSpecBuf)) {
    fixedSpec.Rebind(aSpecBuf, 0);
  } else {
    fixedSpec.Rebind(aURISpec, 0);
  }

  if (aMatchBehavior == mozIPlacesAutoComplete::MATCH_ANYWHERE_UNMODIFIED) {
    return fixedSpec;
  }

  if (StringBeginsWith(fixedSpec, "http://"_ns)) {
    fixedSpec.Rebind(fixedSpec, 7);
  } else if (StringBeginsWith(fixedSpec, "https://"_ns)) {
    fixedSpec.Rebind(fixedSpec, 8);
  } else if (StringBeginsWith(fixedSpec, "ftp://"_ns)) {
    fixedSpec.Rebind(fixedSpec, 6);
  }

  return fixedSpec;
}

/* static */
bool MatchAutoCompleteFunction::findAnywhere(
    const nsDependentCSubstring& aToken, const nsACString& aSourceString) {
  // We can't use FindInReadable here; it works only for ASCII.

  return findInString(aToken, aSourceString, eFindAnywhere);
}

/* static */
bool MatchAutoCompleteFunction::findOnBoundary(
    const nsDependentCSubstring& aToken, const nsACString& aSourceString) {
  return findInString(aToken, aSourceString, eFindOnBoundary);
}

/* static */
MatchAutoCompleteFunction::searchFunctionPtr
MatchAutoCompleteFunction::getSearchFunction(int32_t aBehavior) {
  switch (aBehavior) {
    case mozIPlacesAutoComplete::MATCH_ANYWHERE:
    case mozIPlacesAutoComplete::MATCH_ANYWHERE_UNMODIFIED:
      return findAnywhere;
    case mozIPlacesAutoComplete::MATCH_BOUNDARY:
    default:
      return findOnBoundary;
  };
}

NS_IMPL_ISUPPORTS(MatchAutoCompleteFunction, mozIStorageFunction)

MatchAutoCompleteFunction::MatchAutoCompleteFunction()
    : mCachedZero(new IntegerVariant(0)), mCachedOne(new IntegerVariant(1)) {
  static_assert(IntegerVariant::HasThreadSafeRefCnt::value,
                "Caching assumes that variants have thread-safe refcounting");
}

NS_IMETHODIMP
MatchAutoCompleteFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                          nsIVariant** _result) {
  // Macro to make the code a bit cleaner and easier to read.  Operates on
  // searchBehavior.
  int32_t searchBehavior = aArguments->AsInt32(kArgIndexSearchBehavior);
#define HAS_BEHAVIOR(aBitName) \
  (searchBehavior & mozIPlacesAutoComplete::BEHAVIOR_##aBitName)

  nsDependentCString searchString =
      getSharedUTF8String(aArguments, kArgSearchString);
  nsDependentCString url = getSharedUTF8String(aArguments, kArgIndexURL);

  int32_t matchBehavior = aArguments->AsInt32(kArgIndexMatchBehavior);

  // We only want to filter javascript: URLs if we are not supposed to search
  // for them, and the search does not start with "javascript:".
  if (matchBehavior != mozIPlacesAutoComplete::MATCH_ANYWHERE_UNMODIFIED &&
      StringBeginsWith(url, "javascript:"_ns) && !HAS_BEHAVIOR(JAVASCRIPT) &&
      !StringBeginsWith(searchString, "javascript:"_ns)) {
    *_result = do_AddRef(mCachedZero).take();
    return NS_OK;
  }

  int32_t visitCount = aArguments->AsInt32(kArgIndexVisitCount);
  // Filtering on typed is no more used by Firefox, it is still being used by
  // comm-central clients.
  bool typed = aArguments->AsInt32(kArgIndexTyped) != 0;
  bool bookmark = aArguments->AsInt32(kArgIndexBookmark) != 0;
  nsDependentCString tags = getSharedUTF8String(aArguments, kArgIndexTags);
  int32_t openPageCount = aArguments->AsInt32(kArgIndexOpenPageCount);
  bool matches = false;
  if (HAS_BEHAVIOR(RESTRICT)) {
    // Make sure we match all the filter requirements.  If a given restriction
    // is active, make sure the corresponding condition is not true.
    matches = (!HAS_BEHAVIOR(HISTORY) || visitCount > 0) &&
              (!HAS_BEHAVIOR(TYPED) || typed) &&
              (!HAS_BEHAVIOR(BOOKMARK) || bookmark) &&
              (!HAS_BEHAVIOR(TAG) || !tags.IsVoid()) &&
              (!HAS_BEHAVIOR(OPENPAGE) || openPageCount > 0);
  } else {
    // Make sure that we match all the filter requirements and that the
    // corresponding condition is true if at least a given restriction is
    // active.
    matches = (HAS_BEHAVIOR(HISTORY) && visitCount > 0) ||
              (HAS_BEHAVIOR(TYPED) && typed) ||
              (HAS_BEHAVIOR(BOOKMARK) && bookmark) ||
              (HAS_BEHAVIOR(TAG) && !tags.IsVoid()) ||
              (HAS_BEHAVIOR(OPENPAGE) && openPageCount > 0);
  }

  if (!matches) {
    *_result = do_AddRef(mCachedZero).take();
    return NS_OK;
  }

  // Obtain our search function.
  searchFunctionPtr searchFunction = getSearchFunction(matchBehavior);

  // Clean up our URI spec and prepare it for searching.
  nsCString fixedUrlBuf;
  nsDependentCSubstring fixedUrl =
      fixupURISpec(url, matchBehavior, fixedUrlBuf);
  // Limit the number of chars we search through.
  const nsDependentCSubstring& trimmedUrl =
      Substring(fixedUrl, 0, MAX_CHARS_TO_SEARCH_THROUGH);

  nsDependentCString title = getSharedUTF8String(aArguments, kArgIndexTitle);
  // Limit the number of chars we search through.
  const nsDependentCSubstring& trimmedTitle =
      Substring(title, 0, MAX_CHARS_TO_SEARCH_THROUGH);

  // Caller may pass a fallback title, for example in case of bookmarks or
  // snapshots, one may want to search both the user provided title and the
  // history one.
  nsDependentCString fallbackTitle =
      getSharedUTF8String(aArguments, kArgIndexFallbackTitle);
  // Limit the number of chars we search through.
  const nsDependentCSubstring& trimmedFallbackTitle =
      Substring(fallbackTitle, 0, MAX_CHARS_TO_SEARCH_THROUGH);

  // Determine if every token matches either the bookmark title, tags, page
  // title, or page URL.
  nsCWhitespaceTokenizer tokenizer(searchString);
  while (matches && tokenizer.hasMoreTokens()) {
    const nsDependentCSubstring& token = tokenizer.nextToken();

    if (HAS_BEHAVIOR(TITLE) && HAS_BEHAVIOR(URL)) {
      matches = (searchFunction(token, trimmedTitle) ||
                 searchFunction(token, trimmedFallbackTitle) ||
                 searchFunction(token, tags)) &&
                searchFunction(token, trimmedUrl);
    } else if (HAS_BEHAVIOR(TITLE)) {
      matches = searchFunction(token, trimmedTitle) ||
                searchFunction(token, trimmedFallbackTitle) ||
                searchFunction(token, tags);
    } else if (HAS_BEHAVIOR(URL)) {
      matches = searchFunction(token, trimmedUrl);
    } else {
      matches = searchFunction(token, trimmedTitle) ||
                searchFunction(token, trimmedFallbackTitle) ||
                searchFunction(token, tags) ||
                searchFunction(token, trimmedUrl);
    }
  }

  *_result = do_AddRef(matches ? mCachedOne : mCachedZero).take();
  return NS_OK;
#undef HAS_BEHAVIOR
}

////////////////////////////////////////////////////////////////////////////////
//// Frecency Calculation Function

/* static */
nsresult CalculateFrecencyFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<CalculateFrecencyFunction> function = new CalculateFrecencyFunction();

  nsresult rv = aDBConn->CreateFunction("calculate_frecency"_ns, -1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

CalculateFrecencyFunction::CalculateFrecencyFunction()
    : mSQL(BuildFrecencySQL(
          StaticPrefs::places_frecency_pages_numSampledVisits_AtStartup())) {}

/* static */
nsCString CalculateFrecencyFunction::BuildFrecencySQL(
    uint32_t aNumSampledVisits) {
  /*
    Exponentially decay each visit with an half-life of halfLifeDays.
    Score per each visit is a weight exponentially decayed depending on how
    far away is from a reference date, that is the most recent visit date.
    The weight for each visit is assigned depending on the visit type and other
    information (bookmarked, a redirect, a typed entry).
    If a page has no visits, consider a single visit with an high weight and
    decay its score using the bookmark date as reference time.
    Frecency is the sum of all the scores / number of samples.
    The final score is further decayed using the same half-life.
    To avoid having to decay the score manually, the stored value is the number
    of days after which the score would become 1.

    TODO: Add reference link to source docs here.
  */
  // The LIMIT is inlined rather than bound: SQLite recompiles a statement
  // whenever a parameter used as a LIMIT is rebound or cleared, which would
  // happen on every call. The pref is fixed at startup, so the SQL string
  // and the cached statement stay valid.
  nsCString sql(
      // No index can order the union of the moz_historyvisits and
      // moz_places_metadata rows that visit_interaction samples, so that merge
      // is sorted at run time, over a set bounded by numSampledVisits.
      "/* do not warn (bug 2055967) */ "
      "WITH "
      "lambda (lambda) AS ( "
      "  SELECT ln(2) / :halfLifeDays "
      "), "
      "interactions AS ( "
      "  SELECT "
      "    place_id, "
      "    created_at * 1000 AS visit_date "
      "  FROM "
      "    moz_places_metadata "
      "  WHERE "
      "    place_id = :pageId "
      // Interactions older than the oldest sampled visit, by more than the
      // visit gap, can't be sampled nor make a sampled visit interesting.
      // Skipping them bounds the scan for pages with many interactions.
      "      AND created_at >= IFNULL(( "
      "        SELECT visit_date FROM moz_historyvisits "
      "        WHERE place_id = :pageId AND visit_type NOT IN (7, 8, 9) "
      "        ORDER BY visit_date DESC "
      "        LIMIT 1 OFFSET {numSampledVisits} - 1 "
      "      ) / 1000 - :maxVisitGapSeconds * 1000, 0) "
      // The view time preferences are in seconds while the total view time is
      // in milliseconds.
      "      AND (total_view_time >= :viewTimeSeconds * 1000 "
      "        OR (total_view_time >= :viewTimeIfManyKeypressesSeconds * 1000 "
      "          AND key_presses >= :manyKeypresses)) "
      "  ORDER BY created_at DESC "
      "  LIMIT {numSampledVisits} "
      "), "
      "sampled_visits AS ( "
      "  SELECT "
      "    vs.id, "
      "    vs.from_visit, "
      "    vs.place_id, "
      "    vs.visit_date, "
      "    vs.visit_type, "
      "    vs.source, "
      "    ( "
      "      SELECT EXISTS ( "
      "        SELECT 1 "
      "        FROM interactions i "
      "        WHERE vs.visit_date BETWEEN "
      // Visit dates are in microseconds while the visit gap is in seconds.
      "          i.visit_date - :maxVisitGapSeconds * 1000000 "
      "            AND i.visit_date + :maxVisitGapSeconds * 1000000 "
      "      ) "
      "    ) AS is_interesting "
      "  FROM moz_historyvisits vs "
      "  WHERE place_id = :pageId "
      // Ignore Downloads, Framed Links, and Reloads.
      "    AND vs.visit_type NOT IN (7, 8, 9) "
      // No limit is necessary here as visit_interaction keeps only
      // numSampledVisits most recent rows, and a visit outside the most recent
      // ones here could not survive that.
      "), "
      "virtual_visits AS ( "
      "  SELECT "
      "    NULL AS id, "
      "    0 AS from_visit, "
      "    i.place_id, "
      "    i.visit_date, "
      "    1 AS visit_type, "
      "    0 AS source, "
      "    1 AS is_interesting "
      "  FROM interactions i "
      "  WHERE NOT EXISTS ( "
      "    SELECT 1 FROM moz_historyvisits vs "
      "    WHERE place_id = :pageId "
      "      AND vs.visit_date BETWEEN "
      "        i.visit_date - :maxVisitGapSeconds * 1000000 "
      "        AND i.visit_date + :maxVisitGapSeconds * 1000000 "
      "  ) "
      "), "
      "visit_interaction AS ( "
      "  SELECT * FROM sampled_visits "
      "  UNION ALL "
      "  SELECT * FROM virtual_visits "
      "  ORDER BY visit_date DESC "
      "  LIMIT {numSampledVisits} "
      "), "
      "visits (days, weight) AS ( "
      "  SELECT "
      "    v.visit_date / 86400000000, "
      // A visit is given a score based on a variety of factors, such as
      // whether it was a bookmark, how the user got to the page, and whether
      // or not it was a redirect. The score will be boosted if the visit was
      // interesting and it was not a redirect. A visit is interesting if a user
      // spent a lot of time viewing the page or they typed a lot of keypresses.
      "    (SELECT CASE "
      "      WHEN IFNULL(s.visit_type, v.visit_type) = 3 "  // from a bookmark
      "        OR v.source = 2 "                            // is a bookmark
      "        OR  ( IFNULL(s.visit_type, v.visit_type) = 2 "  // is typed
      "          AND v.source NOT IN (1, 3) "  // not search or sponsored
      "          AND t.id IS NULL AND NOT :isRedirect "  // not a redirect
      "        ) "
      "      THEN "
      "        CASE "
      "          WHEN v.is_interesting = 1 THEN :veryHighWeight "
      "          ELSE :highWeight "
      "        END "
      "      WHEN t.id IS NULL AND NOT :isRedirect "  // not a redirect
      "       AND IFNULL(s.visit_type, v.visit_type) NOT IN (4, 8, 9) "
      "       AND v.source <> 1 "  // Not a sponsored source.
      "      THEN "
      "        CASE "
      "          WHEN v.is_interesting = 1 THEN :highWeight "
      "          ELSE :mediumWeight "
      "         END "
      "      ELSE :lowWeight "
      "     END) "
      "  FROM visit_interaction v "
      // If it's a redirect target, use the visit_type of the source.
      "  LEFT JOIN moz_historyvisits s ON s.id = v.from_visit "
      "                               AND v.visit_type IN (5,6) "
      // If it's a redirect, use a low weight.
      "  LEFT JOIN moz_historyvisits t ON t.from_visit = v.id "
      "                               AND t.visit_type IN (5,6) "
      "), "
      "bookmark (days, weight) AS ( "
      // Use max() to avoid materializing a btree for an ORDER BY 1 LIMIT 1.
      "  SELECT max(dateAdded) / 86400000000, :highWeight "
      "  FROM moz_bookmarks "
      "  WHERE fk = :pageId "
      "  HAVING count(*) > 0 "
      "), "
      "samples (days, weight) AS ( "
      "  SELECT * FROM bookmark WHERE (SELECT count(*) FROM visits) = 0 "
      "  UNION ALL "
      "  SELECT * FROM visits "
      "), "
      "reference (days, samples_count) AS ( "
      "  SELECT max(samples.days), count(*) FROM samples "
      "), "
      "scores (score) AS ( "
      "  SELECT (weight * exp(-lambda * (reference.days - samples.days))) "
      "  FROM samples, reference, lambda "
      ") "
      "SELECT CASE "
      "WHEN (substr(url, 0, 7) = 'place:') THEN 0 "
      "ELSE "
      "  reference.days + CAST (( "
      "    ln( "
      "      sum(score) / samples_count * MAX(visit_count, samples_count) "
      "    ) / lambda "
      "  ) AS INTEGER) "
      "END "
      "FROM moz_places h, reference, lambda, scores "
      "WHERE h.id = :pageId");
  sql.ReplaceSubstring("{numSampledVisits}"_ns,
                       IntToCString(aNumSampledVisits));
  return sql;
}

NS_IMPL_ISUPPORTS(CalculateFrecencyFunction, mozIStorageFunction)

NS_IMETHODIMP
CalculateFrecencyFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                          nsIVariant** _result) {
  // Fetch arguments.  Use default values if they were omitted.
  uint32_t numEntries;
  nsresult rv = aArguments->GetNumEntries(&numEntries);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(numEntries <= 2, "unexpected number of arguments");

  int64_t pageId = aArguments->AsInt64(0);
  MOZ_ASSERT(pageId > 0, "Should always pass a valid page id");
  if (pageId <= 0) {
    *_result = MakeAndAddRef<IntegerVariant>(0).take();
    return NS_OK;
  }

  int32_t isRedirect = 0;
  if (numEntries > 1) {
    isRedirect = aArguments->AsInt32(1);
  }
  // This is a const version of the history object for thread-safety.
  const nsNavHistory* history = nsNavHistory::GetConstHistoryService();
  NS_ENSURE_STATE(history);
  RefPtr<Database> DB = Database::GetDatabase();
  NS_ENSURE_STATE(DB);

  nsCOMPtr<mozIStorageStatement> stmt = DB->GetStatement(mSQL);
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper infoScoper(stmt);

  rv = stmt->BindInt64ByName("pageId"_ns, pageId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName("isRedirect"_ns, isRedirect);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "halfLifeDays"_ns,
      StaticPrefs::places_frecency_pages_halfLifeDays_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "lowWeight"_ns, StaticPrefs::places_frecency_pages_lowWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "mediumWeight"_ns,
      StaticPrefs::places_frecency_pages_mediumWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "highWeight"_ns,
      StaticPrefs::places_frecency_pages_highWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "veryHighWeight"_ns,
      StaticPrefs::places_frecency_pages_veryHighWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "maxVisitGapSeconds"_ns,
      StaticPrefs::
          places_frecency_pages_interactions_maxVisitGapSeconds_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "viewTimeSeconds"_ns,
      StaticPrefs::
          places_frecency_pages_interactions_viewTimeSeconds_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "manyKeypresses"_ns,
      StaticPrefs::
          places_frecency_pages_interactions_manyKeypresses_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "viewTimeIfManyKeypressesSeconds"_ns,
      StaticPrefs::
          places_frecency_pages_interactions_viewTimeIfManyKeypressesSeconds_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult = false;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && hasResult, NS_ERROR_UNEXPECTED);

  bool isNull;
  if (NS_SUCCEEDED(stmt->GetIsNull(0, &isNull)) && isNull) {
    *_result = MakeAndAddRef<IntegerVariant>(0).take();
  } else {
    int32_t score;
    rv = stmt->GetInt32(0, &score);
    NS_ENSURE_SUCCESS(rv, rv);
    *_result = MakeAndAddRef<IntegerVariant>(score).take();
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Frecency Calculation Function

/* static */
nsresult CalculateAltFrecencyFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<CalculateAltFrecencyFunction> function =
      new CalculateAltFrecencyFunction();

  nsresult rv =
      aDBConn->CreateFunction("calculate_alt_frecency"_ns, -1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

CalculateAltFrecencyFunction::CalculateAltFrecencyFunction()
    : mSQL(BuildFrecencySQL(
          StaticPrefs::
              places_frecency_pages_alternative_numSampledVisits_AtStartup())) {
}

/* static */
nsCString CalculateAltFrecencyFunction::BuildFrecencySQL(
    uint32_t aNumSampledVisits) {
  /*
    Exponentially decay each visit with an half-life of halfLifeDays.
    Score per each visit is a weight exponentially decayed depending on how
    far away is from a reference date, that is the most recent visit date.
    The weight for each visit is assigned depending on the visit type and other
    information (bookmarked, a redirect, a typed entry).
    If a page has no visits, consider a single visit with an high weight and
    decay its score using the bookmark date as reference time.
    Frecency is the sum of all the scores / number of samples.
    The final score is further decayed using the same half-life.
    To avoid having to decay the score manually, the stored value is the number
    of days after which the score would become 1.

    TODO: Add reference link to source docs here.
  */
  // See CalculateFrecencyFunction::BuildFrecencySQL for why the LIMIT is
  // inlined.
  nsCString sql(
      // No index can order the union of the moz_historyvisits and
      // moz_places_metadata rows that visit_interaction samples, so that merge
      // is sorted at run time, over a set bounded by numSampledVisits.
      "/* do not warn (bug 2055967) */ "
      "WITH "
      "lambda (lambda) AS ( "
      "  SELECT ln(2) / :halfLifeDays "
      "), "
      "interactions AS ( "
      "  SELECT "
      "    place_id, "
      "    created_at * 1000 AS visit_date "
      "  FROM "
      "    moz_places_metadata "
      "  WHERE "
      "    place_id = :pageId "
      // Interactions older than the oldest sampled visit, by more than the
      // visit gap, can't be sampled nor make a sampled visit interesting.
      // Skipping them bounds the scan for pages with many interactions.
      "      AND created_at >= IFNULL(( "
      "        SELECT visit_date FROM moz_historyvisits "
      "        WHERE place_id = :pageId "
      "        ORDER BY visit_date DESC "
      "        LIMIT 1 OFFSET {numSampledVisits} - 1 "
      "      ) / 1000 - :maxVisitGapSeconds * 1000, 0) "
      // The view time preferences are in seconds while the total view time is
      // in milliseconds.
      "      AND (total_view_time >= :viewTimeSeconds * 1000 "
      "        OR (total_view_time >= :viewTimeIfManyKeypressesSeconds * 1000 "
      "          AND key_presses >= :manyKeypresses)) "
      "  ORDER BY created_at DESC "
      "  LIMIT {numSampledVisits} "
      "), "
      "sampled_visits AS ( "
      "  SELECT "
      "    vs.id, "
      "    vs.from_visit, "
      "    vs.place_id, "
      "    vs.visit_date, "
      "    vs.visit_type, "
      "    vs.source, "
      "    ( "
      "      SELECT EXISTS ( "
      "        SELECT 1 "
      "        FROM interactions i "
      "        WHERE vs.visit_date BETWEEN "
      // Visit dates are in microseconds while the visit gap is in seconds.
      "          i.visit_date - :maxVisitGapSeconds * 1000000 "
      "            AND i.visit_date + :maxVisitGapSeconds * 1000000 "
      "      ) "
      "    ) AS is_interesting "
      "  FROM moz_historyvisits vs "
      "  WHERE place_id = :pageId "
      // No limit is necessary here as visit_interaction keeps only
      // numSampledVisits most recent rows, and a visit outside the most recent
      // ones here could not survive that.
      "), "
      "virtual_visits AS ( "
      "  SELECT "
      "    NULL AS id, "
      "    0 AS from_visit, "
      "    i.place_id, "
      "    i.visit_date, "
      "    1 AS visit_type, "
      "    0 AS source, "
      "    1 AS is_interesting "
      "  FROM interactions i "
      "  WHERE NOT EXISTS ( "
      "    SELECT 1 FROM moz_historyvisits vs "
      "    WHERE  place_id = :pageId "
      "      AND vs.visit_date BETWEEN "
      "        i.visit_date - :maxVisitGapSeconds * 1000000 "
      "        AND i.visit_date + :maxVisitGapSeconds * 1000000 "
      "  ) "
      "), "
      "visit_interaction AS ( "
      "  SELECT * FROM sampled_visits "
      "  UNION ALL "
      "  SELECT * FROM virtual_visits "
      "  ORDER BY visit_date DESC "
      "  LIMIT {numSampledVisits} "
      "), "
      "visits (days, weight) AS ( "
      "  SELECT "
      "    v.visit_date / 86400000000, "
      // A visit is given a score based on a variety of factors, such as
      // whether it was a bookmark, how the user got to the page, and whether
      // or not it was a redirect. The score will be boosted if the visit was
      // interesting and it was not a redirect. A visit is interesting if a user
      // spent a lot of time viewing the page or they typed a lot of keypresses.
      "    (SELECT CASE "
      "      WHEN IFNULL(s.visit_type, v.visit_type) = 3 "  // from a bookmark
      "        OR v.source = 2 "                            // is a bookmark
      "        OR  ( IFNULL(s.visit_type, v.visit_type) = 2 "  // is typed
      "          AND v.source <> 3 "                           // not a search
      "          AND t.id IS NULL AND NOT :isRedirect "        // not a redirect
      "        ) "
      "      THEN "
      "        CASE "
      "          WHEN v.is_interesting = 1 THEN :veryHighWeight "
      "          ELSE :highWeight "
      "        END "
      "      WHEN t.id IS NULL AND NOT :isRedirect "  // not a redirect
      "       AND IFNULL(s.visit_type, v.visit_type) NOT IN (4, 8, 9) "
      "      THEN "
      "        CASE "
      "          WHEN v.is_interesting = 1 THEN :highWeight "
      "          ELSE :mediumWeight "
      "         END "
      "      ELSE :lowWeight "
      "     END) "
      "  FROM visit_interaction v "
      // If it's a redirect target, use the visit_type of the source.
      "  LEFT JOIN moz_historyvisits s ON s.id = v.from_visit "
      "                               AND v.visit_type IN (5,6) "
      // If it's a redirect, use a low weight.
      "  LEFT JOIN moz_historyvisits t ON t.from_visit = v.id "
      "                               AND t.visit_type IN (5,6) "
      "), "
      "bookmark (days, weight) AS ( "
      // Use max() to avoid materializing a btree for an ORDER BY 1 LIMIT 1.
      "  SELECT max(dateAdded) / 86400000000, 100 "
      "  FROM moz_bookmarks "
      "  WHERE fk = :pageId "
      "  HAVING count(*) > 0 "
      "), "
      "samples (days, weight) AS ( "
      "  SELECT * FROM bookmark WHERE (SELECT count(*) FROM visits) = 0 "
      "  UNION ALL "
      "  SELECT * FROM visits "
      "), "
      "reference (days, samples_count) AS ( "
      "  SELECT max(samples.days), count(*) FROM samples "
      "), "
      "scores (score) AS ( "
      "  SELECT (weight * exp(-lambda * (reference.days - samples.days))) "
      "  FROM samples, reference, lambda "
      ") "
      "SELECT CASE "
      "WHEN (substr(url, 0, 7) = 'place:') THEN 0 "
      "ELSE "
      "  reference.days + CAST (( "
      "    ln( "
      "      sum(score) / samples_count * MAX(visit_count, samples_count) "
      "    ) / lambda "
      "  ) AS INTEGER) "
      "END "
      "FROM moz_places h, reference, lambda, scores "
      "WHERE h.id = :pageId");
  sql.ReplaceSubstring("{numSampledVisits}"_ns,
                       IntToCString(aNumSampledVisits));
  return sql;
}

NS_IMPL_ISUPPORTS(CalculateAltFrecencyFunction, mozIStorageFunction)

NS_IMETHODIMP
CalculateAltFrecencyFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                             nsIVariant** _result) {
  // Fetch arguments.  Use default values if they were omitted.
  uint32_t numEntries;
  nsresult rv = aArguments->GetNumEntries(&numEntries);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(numEntries <= 2, "unexpected number of arguments");

  int64_t pageId = aArguments->AsInt64(0);
  MOZ_ASSERT(pageId > 0, "Should always pass a valid page id");
  if (pageId <= 0) {
    *_result = MakeAndAddRef<IntegerVariant>(0).take();
    return NS_OK;
  }

  int32_t isRedirect = 0;
  if (numEntries > 1) {
    isRedirect = aArguments->AsInt32(1);
  }
  // This is a const version of the history object for thread-safety.
  const nsNavHistory* history = nsNavHistory::GetConstHistoryService();
  NS_ENSURE_STATE(history);
  RefPtr<Database> DB = Database::GetDatabase();
  NS_ENSURE_STATE(DB);

  nsCOMPtr<mozIStorageStatement> stmt = DB->GetStatement(mSQL);
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper infoScoper(stmt);

  rv = stmt->BindInt64ByName("pageId"_ns, pageId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName("isRedirect"_ns, isRedirect);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "halfLifeDays"_ns,
      StaticPrefs::places_frecency_pages_alternative_halfLifeDays_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "lowWeight"_ns,
      StaticPrefs::places_frecency_pages_alternative_lowWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "mediumWeight"_ns,
      StaticPrefs::places_frecency_pages_alternative_mediumWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "highWeight"_ns,
      StaticPrefs::places_frecency_pages_alternative_highWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "veryHighWeight"_ns,
      StaticPrefs::
          places_frecency_pages_alternative_veryHighWeight_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "maxVisitGapSeconds"_ns,
      StaticPrefs::
          places_frecency_pages_alternative_interactions_maxVisitGapSeconds_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "viewTimeSeconds"_ns,
      StaticPrefs::
          places_frecency_pages_alternative_interactions_viewTimeSeconds_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "manyKeypresses"_ns,
      StaticPrefs::
          places_frecency_pages_alternative_interactions_manyKeypresses_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(
      "viewTimeIfManyKeypressesSeconds"_ns,
      StaticPrefs::
          places_frecency_pages_alternative_interactions_viewTimeIfManyKeypressesSeconds_AtStartup());
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult = false;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && hasResult, NS_ERROR_UNEXPECTED);

  bool isNull;
  if (NS_SUCCEEDED(stmt->GetIsNull(0, &isNull)) && isNull) {
    *_result = MakeAndAddRef<NullVariant>().take();
  } else {
    int32_t score;
    rv = stmt->GetInt32(0, &score);
    NS_ENSURE_SUCCESS(rv, rv);
    *_result = MakeAndAddRef<IntegerVariant>(score).take();
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// GUID Creation Function

/* static */
nsresult GenerateGUIDFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<GenerateGUIDFunction> function = new GenerateGUIDFunction();
  nsresult rv = aDBConn->CreateFunction("generate_guid"_ns, 0, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(GenerateGUIDFunction, mozIStorageFunction)

NS_IMETHODIMP
GenerateGUIDFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                     nsIVariant** _result) {
  nsAutoCString guid;
  nsresult rv = GenerateGUID(guid);
  NS_ENSURE_SUCCESS(rv, rv);

  *_result = MakeAndAddRef<UTF8TextVariant>(guid).take();
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// GUID Validation Function

/* static */
nsresult IsValidGUIDFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<IsValidGUIDFunction> function = new IsValidGUIDFunction();
  return aDBConn->CreateFunction("is_valid_guid"_ns, 1, function);
}

NS_IMPL_ISUPPORTS(IsValidGUIDFunction, mozIStorageFunction)

NS_IMETHODIMP
IsValidGUIDFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                    nsIVariant** _result) {
  // Must have non-null function arguments.
  MOZ_ASSERT(aArguments);

  nsAutoCString guid;
  aArguments->GetUTF8String(0, guid);

  RefPtr<nsVariant> result = new nsVariant();
  result->SetAsBool(IsValidGUID(guid));
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Get Unreversed Host Function

/* static */
nsresult GetUnreversedHostFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<GetUnreversedHostFunction> function = new GetUnreversedHostFunction();
  nsresult rv = aDBConn->CreateFunction("get_unreversed_host"_ns, 1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(GetUnreversedHostFunction, mozIStorageFunction)

NS_IMETHODIMP
GetUnreversedHostFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                          nsIVariant** _result) {
  // Must have non-null function arguments.
  MOZ_ASSERT(aArguments);

  nsAutoString src;
  aArguments->GetString(0, src);

  RefPtr<nsVariant> result = new nsVariant();

  if (src.Length() > 1) {
    src.Truncate(src.Length() - 1);
    nsAutoString dest;
    ReverseString(src, dest);
    result->SetAsAString(dest);
  } else {
    result->SetAsAString(u""_ns);
  }
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Fixup URL Function

/* static */
nsresult FixupURLFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<FixupURLFunction> function = new FixupURLFunction();
  nsresult rv = aDBConn->CreateFunction("fixup_url"_ns, 1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(FixupURLFunction, mozIStorageFunction)

NS_IMETHODIMP
FixupURLFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                 nsIVariant** _result) {
  // Must have non-null function arguments.
  MOZ_ASSERT(aArguments);

  nsAutoString src;
  aArguments->GetString(0, src);

  RefPtr<nsVariant> result = new nsVariant();

  if (StringBeginsWith(src, u"http://"_ns)) {
    src.Cut(0, 7);
  } else if (StringBeginsWith(src, u"https://"_ns)) {
    src.Cut(0, 8);
  } else if (StringBeginsWith(src, u"ftp://"_ns)) {
    src.Cut(0, 6);
  }

  // Remove common URL hostname prefixes
  if (StringBeginsWith(src, u"www."_ns)) {
    src.Cut(0, 4);
  }

  result->SetAsAString(src);
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Store Last Inserted Id Function

/* static */
nsresult StoreLastInsertedIdFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<StoreLastInsertedIdFunction> function =
      new StoreLastInsertedIdFunction();
  nsresult rv =
      aDBConn->CreateFunction("store_last_inserted_id"_ns, 2, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(StoreLastInsertedIdFunction, mozIStorageFunction)

NS_IMETHODIMP
StoreLastInsertedIdFunction::OnFunctionCall(mozIStorageValueArray* aArgs,
                                            nsIVariant** _result) {
  uint32_t numArgs;
  nsresult rv = aArgs->GetNumEntries(&numArgs);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(numArgs == 2);

  nsAutoCString table;
  rv = aArgs->GetUTF8String(0, table);
  NS_ENSURE_SUCCESS(rv, rv);

  int64_t lastInsertedId = aArgs->AsInt64(1);

  MOZ_ASSERT(table.EqualsLiteral("moz_places") ||
             table.EqualsLiteral("moz_historyvisits") ||
             table.EqualsLiteral("moz_bookmarks") ||
             table.EqualsLiteral("moz_icons"));

  if (table.EqualsLiteral("moz_bookmarks")) {
    nsNavBookmarks::StoreLastInsertedId(table, lastInsertedId);
  } else if (table.EqualsLiteral("moz_icons")) {
    nsFaviconService::StoreLastInsertedId(table, lastInsertedId);
  } else {
    nsNavHistory::StoreLastInsertedId(table, lastInsertedId);
  }

  RefPtr<nsVariant> result = new nsVariant();
  rv = result->SetAsInt64(lastInsertedId);
  NS_ENSURE_SUCCESS(rv, rv);
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Get Query Param Function

/* static */
nsresult GetQueryParamFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<GetQueryParamFunction> function = new GetQueryParamFunction();
  return aDBConn->CreateFunction("get_query_param"_ns, 2, function);
}

NS_IMPL_ISUPPORTS(GetQueryParamFunction, mozIStorageFunction)

NS_IMETHODIMP
GetQueryParamFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                      nsIVariant** _result) {
  // Must have non-null function arguments.
  MOZ_ASSERT(aArguments);

  nsDependentCString queryString = getSharedUTF8String(aArguments, 0);
  nsDependentCString paramName = getSharedUTF8String(aArguments, 1);

  RefPtr<nsVariant> result = new nsVariant();
  if (!queryString.IsEmpty() && !paramName.IsEmpty()) {
    URLParams::Parse(queryString, true,
                     [&paramName, &result](const nsACString& aName,
                                           const nsACString& aValue) {
                       if (!paramName.Equals(aName)) {
                         return true;
                       }
                       result->SetAsACString(aValue);
                       return false;
                     });
  }

  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Hash Function

/* static */
nsresult HashFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<HashFunction> function = new HashFunction();
  return aDBConn->CreateFunction("hash"_ns, -1, function);
}

NS_IMPL_ISUPPORTS(HashFunction, mozIStorageFunction)

NS_IMETHODIMP
HashFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                             nsIVariant** _result) {
  // Must have non-null function arguments.
  MOZ_ASSERT(aArguments);

  // Fetch arguments.  Use default values if they were omitted.
  uint32_t numEntries;
  nsresult rv = aArguments->GetNumEntries(&numEntries);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(numEntries >= 1 && numEntries <= 2, NS_ERROR_FAILURE);

  nsDependentCString str = getSharedUTF8String(aArguments, 0);
  nsAutoCString mode;
  if (numEntries > 1) {
    aArguments->GetUTF8String(1, mode);
  }

  RefPtr<nsVariant> result = new nsVariant();
  uint64_t hash;
  rv = mozilla::places::HashURL(str, mode, &hash);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = result->SetAsInt64((int64_t)hash);
  NS_ENSURE_SUCCESS(rv, rv);

  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// SHA256Hex Function

/* static */
nsresult SHA256HexFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<SHA256HexFunction> function = new SHA256HexFunction();
  return aDBConn->CreateFunction("sha256hex"_ns, -1, function);
}

NS_IMPL_ISUPPORTS(SHA256HexFunction, mozIStorageFunction)

NS_IMETHODIMP
SHA256HexFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                  nsIVariant** _result) {
  // Must have non-null function arguments.
  MOZ_ASSERT(aArguments);

  // Fetch arguments.
  uint32_t numEntries;
  nsresult rv = aArguments->GetNumEntries(&numEntries);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(numEntries == 1, NS_ERROR_FAILURE);
  nsDependentCString str = getSharedUTF8String(aArguments, 0);

  nsCOMPtr<nsICryptoHash> hasher =
      do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  // SHA256 is not super strong but fine for our mapping needs.
  rv = hasher->Init(nsICryptoHash::SHA256);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = hasher->Update(reinterpret_cast<const uint8_t*>(str.BeginReading()),
                      str.Length());
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString binaryHash, hashString;
  rv = hasher->Finish(false, binaryHash);
  NS_ENSURE_SUCCESS(rv, rv);

  // Convert to HEX.
  static const char* const hex = "0123456789abcdef";
  hashString.SetCapacity(2 * binaryHash.Length());
  for (size_t i = 0; i < binaryHash.Length(); ++i) {
    auto c = static_cast<unsigned char>(binaryHash[i]);
    hashString.Append(hex[(c >> 4) & 0x0F]);
    hashString.Append(hex[c & 0x0F]);
  }

  RefPtr<nsVariant> result = new nsVariant();
  result->SetAsACString(hashString);
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Get prefix function

/* static */
nsresult GetPrefixFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<GetPrefixFunction> function = new GetPrefixFunction();
  nsresult rv = aDBConn->CreateFunction("get_prefix"_ns, 1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(GetPrefixFunction, mozIStorageFunction)

NS_IMETHODIMP
GetPrefixFunction::OnFunctionCall(mozIStorageValueArray* aArgs,
                                  nsIVariant** _result) {
  MOZ_ASSERT(aArgs);

  uint32_t numArgs;
  nsresult rv = aArgs->GetNumEntries(&numArgs);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(numArgs == 1);

  nsDependentCString spec(getSharedUTF8String(aArgs, 0));

  RefPtr<nsVariant> result = new nsVariant();
  result->SetAsACString(Substring(spec, 0, getPrefixLength(spec)));
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Get host and port function

/* static */
nsresult GetHostAndPortFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<GetHostAndPortFunction> function = new GetHostAndPortFunction();
  nsresult rv = aDBConn->CreateFunction("get_host_and_port"_ns, 1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(GetHostAndPortFunction, mozIStorageFunction)

NS_IMETHODIMP
GetHostAndPortFunction::OnFunctionCall(mozIStorageValueArray* aArgs,
                                       nsIVariant** _result) {
  MOZ_ASSERT(aArgs);

  uint32_t numArgs;
  nsresult rv = aArgs->GetNumEntries(&numArgs);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(numArgs == 1);

  nsDependentCString spec(getSharedUTF8String(aArgs, 0));

  RefPtr<nsVariant> result = new nsVariant();

  size_type length;
  size_type index = indexOfHostAndPort(spec, &length);
  result->SetAsACString(Substring(spec, index, length));
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Strip prefix and userinfo function

/* static */
nsresult StripPrefixAndUserinfoFunction::create(
    mozIStorageConnection* aDBConn) {
  RefPtr<StripPrefixAndUserinfoFunction> function =
      new StripPrefixAndUserinfoFunction();
  nsresult rv =
      aDBConn->CreateFunction("strip_prefix_and_userinfo"_ns, 1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(StripPrefixAndUserinfoFunction, mozIStorageFunction)

NS_IMETHODIMP
StripPrefixAndUserinfoFunction::OnFunctionCall(mozIStorageValueArray* aArgs,
                                               nsIVariant** _result) {
  MOZ_ASSERT(aArgs);

  uint32_t numArgs;
  nsresult rv = aArgs->GetNumEntries(&numArgs);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(numArgs == 1);

  nsDependentCString spec(getSharedUTF8String(aArgs, 0));

  RefPtr<nsVariant> result = new nsVariant();

  size_type index = indexOfHostAndPort(spec, nullptr);
  result->SetAsACString(Substring(spec, index, spec.Length() - index));
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Should start frecency recalculation function

/* static */
nsresult SetShouldStartFrecencyRecalculationFunction::create(
    mozIStorageConnection* aDBConn) {
  RefPtr<SetShouldStartFrecencyRecalculationFunction> function =
      new SetShouldStartFrecencyRecalculationFunction();
  nsresult rv = aDBConn->CreateFunction(
      "set_should_start_frecency_recalculation"_ns, 0, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(SetShouldStartFrecencyRecalculationFunction,
                  mozIStorageFunction)

NS_IMETHODIMP
SetShouldStartFrecencyRecalculationFunction::OnFunctionCall(
    mozIStorageValueArray* aArgs, nsIVariant** _result) {
  MOZ_ASSERT(aArgs);

#ifdef DEBUG
  uint32_t numArgs;
  MOZ_ASSERT(NS_SUCCEEDED(aArgs->GetNumEntries(&numArgs)) && numArgs == 0);
#endif

  // When changing from false to true, dispatch a runnable to the main-thread
  // to start a recalculation. Once there's nothing left to recalculathe this
  // boolean will be set back to false. Note this means there will be a short
  // interval between completing a recalculation and setting this back to false
  // where we could potentially lose a recalculation request. That should not be
  // a big deal, since the recalculation will just happen at the next operation
  // changing frecency or, in the worst case, at the next session.
  if (!nsNavHistory::sShouldStartFrecencyRecalculation.exchange(true)) {
    (void)NS_DispatchToMainThread(NS_NewRunnableFunction(
        "SetShouldStartFrecencyRecalculationFunction::Notify", [] {
          nsCOMPtr<nsIObserverService> os = services::GetObserverService();
          if (os) {
            (void)os->NotifyObservers(nullptr, "frecency-recalculation-needed",
                                      nullptr);
          }
        }));
  }

  RefPtr<nsVariant> result = new nsVariant();
  nsresult rv = result->SetAsBool(true);
  NS_ENSURE_SUCCESS(rv, rv);
  result.forget(_result);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Note Sync Change Function

/* static */
nsresult NoteSyncChangeFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<NoteSyncChangeFunction> function = new NoteSyncChangeFunction();
  nsresult rv = aDBConn->CreateFunction("note_sync_change"_ns, 0, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(NoteSyncChangeFunction, mozIStorageFunction)

NS_IMETHODIMP
NoteSyncChangeFunction::OnFunctionCall(mozIStorageValueArray* aArgs,
                                       nsIVariant** _result) {
  nsNavBookmarks::NoteSyncChange();
  *_result = nullptr;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Invalidate days of history Function

/* static */
nsresult InvalidateDaysOfHistoryFunction::create(
    mozIStorageConnection* aDBConn) {
  RefPtr<InvalidateDaysOfHistoryFunction> function =
      new InvalidateDaysOfHistoryFunction();
  nsresult rv =
      aDBConn->CreateFunction("invalidate_days_of_history"_ns, 0, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(InvalidateDaysOfHistoryFunction, mozIStorageFunction)

NS_IMETHODIMP
InvalidateDaysOfHistoryFunction::OnFunctionCall(mozIStorageValueArray* aArgs,
                                                nsIVariant** _result) {
  nsNavHistory::InvalidateDaysOfHistory();
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// Target folder guid from places query Function

/* static */
nsresult TargetFolderGuidFunction::create(mozIStorageConnection* aDBConn) {
  RefPtr<TargetFolderGuidFunction> function = new TargetFolderGuidFunction();
  nsresult rv = aDBConn->CreateFunction("target_folder_guid"_ns, 1, function);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(TargetFolderGuidFunction, mozIStorageFunction)

NS_IMETHODIMP
TargetFolderGuidFunction::OnFunctionCall(mozIStorageValueArray* aArguments,
                                         nsIVariant** _result) {
  // Must have non-null function arguments.
  MOZ_ASSERT(aArguments);
  // Must have one argument.
  DebugOnly<uint32_t> numArgs = 0;
  MOZ_ASSERT(NS_SUCCEEDED(aArguments->GetNumEntries(&numArgs)) && numArgs == 1,
             "unexpected number of arguments");

  nsDependentCString queryURI = getSharedUTF8String(aArguments, 0);
  Maybe<nsCString> targetFolderGuid =
      nsNavHistory::GetTargetFolderGuid(queryURI);

  if (targetFolderGuid.isSome()) {
    RefPtr<nsVariant> result = new nsVariant();
    result->SetAsACString(*targetFolderGuid);
    result.forget(_result);
  } else {
    *_result = MakeAndAddRef<NullVariant>().take();
  }

  return NS_OK;
}

}  // namespace mozilla::places
