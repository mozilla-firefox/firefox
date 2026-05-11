/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/intl/LocaleNegotiation.h"

#include "mozilla/Assertions.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/Collator.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/NumberingSystem.h"
#include "mozilla/Maybe.h"
#include "mozilla/Range.h"

#include <algorithm>
#include <array>
#include <stddef.h>
#include <utility>

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/NumberingSystemsGenerated.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "builtin/intl/SharedIntlData.h"
#include "builtin/intl/StringAsciiChars.h"
#include "js/Conversions.h"
#include "js/GCAPI.h"
#include "js/Result.h"
#include "util/StringBuilder.h"
#include "vm/ArrayObject.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"
#include "vm/Realm.h"
#include "vm/StringType.h"

#include "vm/NativeObject-inl.h"
#include "vm/ObjectOperations-inl.h"

using namespace js;
using namespace js::intl;

static constexpr auto UnicodeExtensionKeyNames() {
  mozilla::EnumeratedArray<UnicodeExtensionKey, const char*> names;
  names[UnicodeExtensionKey::Calendar] = "ca";
  names[UnicodeExtensionKey::Collation] = "co";
  names[UnicodeExtensionKey::CollationCaseFirst] = "kf";
  names[UnicodeExtensionKey::CollationNumeric] = "kn";
  names[UnicodeExtensionKey::FirstDayOfWeek] = "fw";
  names[UnicodeExtensionKey::HourCycle] = "hc";
  names[UnicodeExtensionKey::NumberingSystem] = "nu";
  return names;
}

template <typename CharT>
static mozilla::Maybe<UnicodeExtensionKey> ToUnicodeExtensionKey(
    std::basic_string_view<CharT> subtag) {
  MOZ_ASSERT(subtag.length() == 2);

  static constexpr auto names = UnicodeExtensionKeyNames();
  for (auto key : mozilla::MakeInclusiveEnumeratedRange(
           mozilla::MaxEnumValue<UnicodeExtensionKey>::value)) {
    const auto* name = names[key];
    if (name[0] == subtag[0] && name[1] == subtag[1]) {
      return mozilla::Some(key);
    }
  }
  return mozilla::Nothing();
}

static void AssertCanonicalLocale(JSContext* cx, const JSLinearString* locale) {
#ifdef DEBUG
  MOZ_ASSERT(StringIsAscii(locale), "language tags are ASCII-only");

  // |locale| is a structurally valid language tag.
  mozilla::intl::Locale tag;

  using ParserError = mozilla::intl::LocaleParser::ParserError;
  mozilla::Result<mozilla::Ok, ParserError> parse_result = Ok();
  {
    StringAsciiChars chars(locale);
    if (!chars.init(cx)) {
      cx->recoverFromOutOfMemory();
      return;
    }

    parse_result = mozilla::intl::LocaleParser::TryParse(chars, tag);
  }

  if (parse_result.isErr()) {
    MOZ_ASSERT(parse_result.unwrapErr() == ParserError::OutOfMemory,
               "locale is a structurally valid language tag");
    return;
  }

  auto canonicalizeResult = [&] {
    // Tell the analysis this function can't GC. (bug 1588528)
    JS::AutoSuppressGCAnalysis nogc;

    return tag.Canonicalize();
  }();
  if (canonicalizeResult.isErr()) {
    MOZ_ASSERT(canonicalizeResult.unwrapErr() !=
               mozilla::intl::Locale::CanonicalizationError::DuplicateVariant);
    return;
  }

  FormatBuffer<char, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  if (auto result = tag.ToString(buffer); result.isErr()) {
    cx->recoverFromOutOfMemory();
    return;
  }

  MOZ_ASSERT(StringEqualsAscii(locale, buffer.data(), buffer.length()),
             "locale is a canonicalized language tag");
#endif
}

mozilla::Maybe<LanguageId> js::intl::ToLanguageId(
    JSContext* cx, const JSLinearString* locale) {
  AssertCanonicalLocale(cx, locale);

  // Tell the analysis the |ToLanguageId| function can't GC. (bug 1588528)
  JS::AutoSuppressGCAnalysis nogc;
  auto parsedLangId =
      locale->hasLatin1Chars()
          ? LanguageId::fromBcp49(mozilla::AsChars(locale->latin1Range(nogc)))
          : LanguageId::fromBcp49(mozilla::Span{locale->twoByteRange(nogc)});
  return parsedLangId.map([](const auto& pair) { return pair.first; });
}

/**
 * 9.2.2 BestAvailableLocale ( availableLocales, locale )
 *
 * Compares a BCP 47 language tag against the locales in availableLocales and
 * returns the best available match. Uses the fallback mechanism of RFC 4647,
 * section 3.4.
 *
 * Spec: ECMAScript Internationalization API Specification, 9.2.2.
 * Spec: RFC 4647, section 3.4.
 */
static bool BestAvailableLocale(JSContext* cx,
                                AvailableLocaleKind availableLocales,
                                LanguageId locale,
                                mozilla::Maybe<LanguageId> defaultLocale,
                                mozilla::Maybe<LanguageId>* result) {
  // In the spec, [[availableLocales]] is formally a list of all available
  // locales. But in our implementation, it's an *incomplete* list, not
  // necessarily including the default locale (and all locales implied by it,
  // e.g. "de" implied by "de-CH"), if that locale isn't in every
  // [[availableLocales]] list (because that locale is supported through
  // fallback, e.g. "de-CH" supported through "de").
  //
  // If we're considering the default locale, augment the spec loop with
  // additional checks to also test whether the current prefix is a prefix of
  // the default locale.

  auto& sharedIntlData = cx->runtime()->sharedIntlData.ref();

  // Step 1.
  auto candidate = locale;

  // Step 2.
  while (candidate != LanguageId::und()) {
    // Step 2.a.
    bool supported = false;
    if (!sharedIntlData.isAvailableLocale(cx, availableLocales, candidate,
                                          &supported)) {
      return false;
    }
    if (supported) {
      *result = mozilla::Some(candidate);
      return true;
    }

    if (defaultLocale && candidate.isPrefixOf(*defaultLocale)) {
      *result = mozilla::Some(candidate);
      return true;
    }

    // Steps 2.b-d.
    candidate = candidate.parentLocale();
  }

  *result = mozilla::Nothing();
  return true;
}

/**
 * 9.2.2 BestAvailableLocale ( availableLocales, locale )
 */
static bool BestAvailableLocale(JSContext* cx,
                                AvailableLocaleKind availableLocales,
                                Handle<JSLinearString*> locale,
                                mozilla::Maybe<LanguageId> defaultLocale,
                                mozilla::Maybe<LanguageId>* result) {
  auto langId = ToLanguageId(cx, locale);

  // Reject locales with overlong language subtags.
  if (!langId) {
    *result = mozilla::Nothing();
    return true;
  }

  // Variant and extension subtags in |locale| are ignored, because all
  // supported available locales only consist of language, script, and region
  // subtags.
  return BestAvailableLocale(cx, availableLocales, *langId, defaultLocale,
                             result);
}

/**
 * 9.2.2 BestAvailableLocale ( availableLocales, locale )
 */
static bool BestAvailableLocale(JSContext* cx,
                                AvailableLocaleKind availableLocales,
                                LanguageId locale,
                                mozilla::Maybe<LanguageId>* result) {
  return BestAvailableLocale(cx, availableLocales, locale, mozilla::Nothing(),
                             result);
}

/**
 * Returns the subset of requestedLocales for which availableLocales has a
 * matching (possibly fallback) locale. Locales appear in the same order in the
 * returned list as in the input list.
 *
 * Spec: ECMAScript Internationalization API Specification, 9.2.7.
 * Spec: ECMAScript Internationalization API Specification, 9.2.8.
 */
static bool LookupSupportedLocales(
    JSContext* cx, AvailableLocaleKind availableLocales,
    Handle<LocalesList> requestedLocales,
    MutableHandle<LocalesList> supportedLocales) {
  // Step 1.
  MOZ_ASSERT(supportedLocales.empty());

  auto defaultLocale = LanguageId::und();
  if (!DefaultLocale(cx, &defaultLocale)) {
    return false;
  }

  // Step 2.
  for (size_t i = 0; i < requestedLocales.length(); i++) {
    auto locale = requestedLocales[i];

    // Steps 2.a-b.
    mozilla::Maybe<LanguageId> availableLocale{};
    if (!BestAvailableLocale(cx, availableLocales, locale,
                             mozilla::Some(defaultLocale), &availableLocale)) {
      return false;
    }

    // Step 2.c.
    if (availableLocale) {
      if (!supportedLocales.append(locale)) {
        return false;
      }
    }
  }

  // Step 3.
  return true;
}

/**
 * Returns the subset of requestedLocales for which availableLocales has a
 * matching (possibly fallback) locale. Locales appear in the same order in the
 * returned list as in the input list.
 *
 * Spec: ECMAScript Internationalization API Specification, 9.2.9.
 */
static bool SupportedLocales(JSContext* cx,
                             AvailableLocaleKind availableLocales,
                             Handle<LocalesList> requestedLocales,
                             Handle<Value> options,
                             MutableHandle<LocalesList> supportedLocales) {
  // Step 1.
  if (!options.isUndefined()) {
    // Step 1.a.
    Rooted<JSObject*> obj(cx, ToObject(cx, options));
    if (!obj) {
      return false;
    }

    // Step 1.b.
    LocaleMatcher localeMatcher;
    if (!GetLocaleMatcherOption(cx, obj, JSMSG_INVALID_LOCALE_MATCHER,
                                &localeMatcher)) {
      return false;
    }
  }

  // Steps 2-5.
  //
  // We don't yet support anything better than the lookup matcher.
  return LookupSupportedLocales(cx, availableLocales, requestedLocales,
                                supportedLocales);
}

/**
 * Returns the start and end indices of a "Unicode locale extension sequence",
 * which the specification defines as: "any substring of a language tag that
 * starts with a separator '-' and the singleton 'u' and includes the maximum
 * sequence of following non-singleton subtags and their preceding '-'
 * separators."
 *
 * Alternatively, this may be defined as: the components of a language tag that
 * match the `unicode_locale_extensions` production in UTS 35.
 *
 * Spec: ECMAScript Internationalization API Specification, 6.2.1.
 */
template <typename CharT>
static std::pair<size_t, size_t> FindUnicodeExtensionSequence(
    std::basic_string_view<CharT> locale) {
  // Return early if the locale string is too small to hold any Unicode
  // extension sequences. (This is the common case, so handle it first.)
  //
  // Smallest language subtag has two characters.
  // Smallest Unicode extension sequence has five characters
  if (locale.length() < (2 + 5)) {
    return {};
  }

  // Search for the start of a Unicode extension sequence.
  //
  // Begin searching after the smallest possible language subtag, namely
  // |2alpha|. End searching once the remaining characters can't fit the
  // smallest possible Unicode extension sequence, namely |"-u-" 2alphanum|.
  // Note the reduced end-limit means indexing inside the loop is always
  // in-range.
  size_t start = 0;
  for (size_t i = 2; i <= locale.length() - 5; i++) {
    // Search for "-u-" marking the start of a Unicode extension sequence.
    if (locale[i] == '-' && locale[i + 1] == 'u' && locale[i + 2] == '-') {
      start = i;
      break;
    }

    // And search for "-x-" marking the start of any privateuse component to
    // handle the case when "-u-" was only found within a privateuse subtag.
    if (locale[i] == '-' && locale[i + 1] == 'x' && locale[i + 2] == '-') {
      break;
    }
  }

  // Return if no Unicode extension sequence was found.
  if (start == 0) {
    return {};
  }

  // Search for the start of the next singleton or privateuse subtag.
  //
  // Begin searching after the smallest possible Unicode locale extension
  // sequence, namely |"-u-" 2alphanum|. End searching once the remaining
  // characters can't fit the smallest possible privateuse subtag, namely
  // |"-x-" alphanum|. Note the reduced end-limit means indexing inside the loop
  // is always in-range.
  for (size_t i = start + 5; i <= locale.length() - 4; i++) {
    if (locale[i] != '-') {
      continue;
    }
    if (locale[i + 2] == '-') {
      return {start, i};
    }

    // Skip over (i + 1) and (i + 2) because we've just verified they aren't
    // "-", so the next possible delimiter can only be at (i + 3).
    i += 2;
  }

  // If no singleton or privateuse subtag was found, the Unicode extension
  // sequence extends until the end of the string.
  return {start, locale.length()};
}

class LookupMatcherResult final {
  LanguageId locale_ = LanguageId::und();
  JSLinearString* requestedLocale_ = nullptr;

 public:
  LookupMatcherResult() = default;
  LookupMatcherResult(LanguageId locale, JSLinearString* requestedLocale)
      : locale_(locale), requestedLocale_(requestedLocale) {}

  auto locale() const { return locale_; }
  auto* requestedLocale() const { return requestedLocale_; }

  // Helper method for WrappedPtrOperations.
  auto requestedLocaleDoNotUse() const { return &requestedLocale_; }

  // Trace implementation.
  void trace(JSTracer* trc);
};

void LookupMatcherResult::trace(JSTracer* trc) {
  TraceRoot(trc, &requestedLocale_, "LookupMatcherResult::requestedLocale");
}

namespace js {
template <typename Wrapper>
class WrappedPtrOperations<LookupMatcherResult, Wrapper> {
  const auto& container() const {
    return static_cast<const Wrapper*>(this)->get();
  }

 public:
  LanguageId locale() const { return container().locale(); }

  JS::Handle<JSLinearString*> requestedLocale() const {
    return JS::Handle<JSLinearString*>::fromMarkedLocation(
        container().requestedLocaleDoNotUse());
  }
};
}  // namespace js

/**
 * LookupMatchingLocaleByPrefix ( availableLocales, requestedLocales )
 *
 * Compares a BCP 47 language priority list against the set of locales in
 * availableLocales and determines the best available language to meet the
 * request. Options specified through Unicode extension subsequences are
 * ignored in the lookup, but information about such subsequences is returned
 * separately.
 *
 * This variant is based on the Lookup algorithm of RFC 4647 section 3.4.
 *
 * Spec: ECMAScript Internationalization API Specification, 9.2.3.
 * Spec: RFC 4647, section 3.4.
 */
static bool LookupMatcher(JSContext* cx, AvailableLocaleKind availableLocales,
                          Handle<ArrayObject*> locales,
                          MutableHandle<LookupMatcherResult> result) {
  MOZ_RELEASE_ASSERT(IsPackedArray(locales));

  auto defaultLocale = LanguageId::und();
  if (!DefaultLocale(cx, &defaultLocale)) {
    return false;
  }

  // Step 1. (Not applicable)

  // Step 2.
  Rooted<JSLinearString*> locale(cx);
  for (size_t i = 0, length = locales->length(); i < length; i++) {
    locale = locales->getDenseElement(i).toString()->ensureLinear(cx);
    if (!locale) {
      return false;
    }

    // Steps 2.a-b.
    mozilla::Maybe<LanguageId> availableLocale{};
    if (!BestAvailableLocale(cx, availableLocales, locale,
                             mozilla::Some(defaultLocale), &availableLocale)) {
      return false;
    }

    // Step 2.c.
    if (availableLocale) {
      // Steps 2.c.i-ii. (Not applicable)

      // Step 2.c.iii.
      result.set({*availableLocale, locale});
      return true;
    }
  }

  // Steps 3-5.
  result.set({defaultLocale, nullptr});
  return true;
}

bool js::intl::LookupMatcher(JSContext* cx,
                             AvailableLocaleKind availableLocales,
                             LanguageId locale,
                             mozilla::Maybe<LanguageId>* result) {
  auto defaultLocale = LanguageId::und();
  if (!DefaultLocale(cx, &defaultLocale)) {
    return false;
  }

  return BestAvailableLocale(cx, availableLocales, locale,
                             mozilla::Some(defaultLocale), result);
}

void js::intl::LocaleOptions::trace(JSTracer* trc) {
  for (auto& extension : extensions_) {
    TraceRoot(trc, &extension, "LocaleOptions::extension");
  }
}

JSLinearString* js::intl::ResolvedLocale::toLocale(JSContext* cx) const {
  auto dataLocaleStr = dataLocale_.toString();

  if (keywords_.isEmpty()) {
    return NewStringCopy<CanGC>(cx, std::string_view{dataLocaleStr});
  }

  JSStringBuilder sb(cx);
  if (!sb.append(dataLocaleStr.data(), dataLocaleStr.length())) {
    return nullptr;
  }
  if (!sb.append("-u")) {
    return nullptr;
  }
  for (auto key : keywords_) {
    static constexpr auto names = UnicodeExtensionKeyNames();

    if (!sb.append('-') || !sb.append(names[key], 2)) {
      return nullptr;
    }

    auto* extension = extensions_[key];
    MOZ_ASSERT(extension);

    if (!extension->empty() && !StringEqualsLiteral(extension, "true")) {
      if (!sb.append('-') || !sb.append(extension)) {
        return nullptr;
      }
    }
  }
  return sb.finishString();
}

void js::intl::ResolvedLocale::trace(JSTracer* trc) {
  for (auto& extension : extensions_) {
    TraceRoot(trc, &extension, "ResolvedLocale::extension");
  }
}

/**
 * Unicode extension keywords found by UnicodeExtensionComponents.
 */
class UnicodeExtensionKeywords {
  // Start position and length of a Unicode extension keyword.
  using Value = std::pair<size_t, size_t>;

  mozilla::EnumeratedArray<UnicodeExtensionKey, Value> keywords;

 public:
  /**
   * Return `true` if the Unicode extension |key| is present.
   */
  bool has(UnicodeExtensionKey key) const { return keywords[key].first > 0; }

  /**
   * Get the Unicode extension for the argument |key|.
   */
  const auto& get(UnicodeExtensionKey key) const { return keywords[key]; }

  /**
   * Get a mutable reference to the Unicode extension for the argument |key|.
   */
  auto& get(UnicodeExtensionKey key) { return keywords[key]; }
};

/**
 * UnicodeExtensionComponents ( extension )
 */
template <typename CharT>
static auto UnicodeExtensionComponents(std::basic_string_view<CharT> locale) {
  // Search for Unicode extension sequences in |locale|.
  auto [startOfUnicodeExtensions, endOfUnicodeExtensions] =
      FindUnicodeExtensionSequence(locale);

  // Return early if |locale| contains no Unicode extension sequences.
  if (!startOfUnicodeExtensions) {
    return UnicodeExtensionKeywords{};
  }

  // Extract the Unicode extension sequence of |locale|.
  MOZ_ASSERT(startOfUnicodeExtensions < endOfUnicodeExtensions);
  MOZ_ASSERT(endOfUnicodeExtensions <= locale.length());

  auto extension =
      locale.substr(startOfUnicodeExtensions,
                    endOfUnicodeExtensions - startOfUnicodeExtensions);

  // Step 1.
  MOZ_ASSERT(std::all_of(extension.begin(), extension.end(), [](auto ch) {
    return mozilla::IsAscii(ch) && !mozilla::IsAsciiUppercaseAlpha(ch);
  }));

  // Step 2.
  MOZ_ASSERT(extension.length() >= 5);
  MOZ_ASSERT(extension[0] == '-');
  MOZ_ASSERT(extension[1] == 'u');
  MOZ_ASSERT(extension[2] == '-');

  // Step 3. (Not applicable in our implementation.)

  // Step 4.
  UnicodeExtensionKeywords keywords{};

  // Step 5.
  mozilla::Maybe<UnicodeExtensionKey> key{};

  // Steps 6-8.
  for (size_t k = 3; k < extension.length();) {
    // Step 8.a.
    size_t e = extension.find('-', k);

    // Step 8.b.
    size_t len = (e == extension.npos ? extension.length() : e) - k;

    // Step 8.c.
    auto subtag = extension.substr(k, len);

    // Steps 8.d-e.
    MOZ_ASSERT(len >= 2);

    // Steps 8.f-i
    if (len == 2) {
      key = ToUnicodeExtensionKey(subtag);

      if (key && !keywords.has(*key)) {
        // Record keyword start position.
        keywords.get(*key) = {startOfUnicodeExtensions + k + 3, 0};
      } else {
        // Ignore duplicate or irrelevant keywords.
        key = mozilla::Nothing();
      }
    } else if (key) {
      // Update keyword length.
      auto& keyword = keywords.get(*key);
      if (keyword.second == 0) {
        keyword.second = len;
      } else {
        keyword.second += 1 + len;
      }
    }

    // Step 8.j.
    k = k + len + 1;
  }

  // Step 9.
  return keywords;
}

/**
 * UnicodeExtensionComponents ( extension )
 */
static bool CanHaveUnicodeExtensionComponents(const JSLinearString* locale) {
  // Smallest language subtag has two characters.
  // Smallest Unicode extension sequence has five characters
  constexpr size_t minLength = 2 + 5;

  // NB: |locale| can be nullptr when the default locale is used.
  return locale && locale->length() >= minLength;
}

/**
 * UnicodeExtensionComponents ( extension )
 */
static auto UnicodeExtensionComponents(const JSLinearString* locale) {
  MOZ_ASSERT(CanHaveUnicodeExtensionComponents(locale));
  MOZ_ASSERT(StringIsAscii(locale));

  JS::AutoCheckCannotGC nogc;

  if (locale->hasLatin1Chars()) {
    const auto* chars = locale->latin1Chars(nogc);
    std::string_view sv{reinterpret_cast<const char*>(chars), locale->length()};
    return UnicodeExtensionComponents(sv);
  }

  const auto* chars = locale->twoByteChars(nogc);
  std::u16string_view sv{chars, locale->length()};
  return UnicodeExtensionComponents(sv);
}

/**
 * Return `true` in |result| iff `string` is a supported calendar for the
 * requested locale. Otherwise set |result| to `false`.
 */
static bool IsSupportedCalendar(JSContext* cx, LanguageId locale,
                                Handle<JSLinearString*> string, bool* result) {
  MOZ_ASSERT(StringIsAscii(string));

  auto keywords = mozilla::intl::Calendar::GetBcp47KeywordValuesForLocale(
      locale.toString().c_str());
  if (keywords.isErr()) {
    ReportInternalError(cx, keywords.unwrapErr());
    return false;
  }

  for (auto keyword : keywords.unwrap()) {
    if (keyword.isErr()) {
      ReportInternalError(cx);
      return false;
    }
    auto calendar = keyword.unwrap();

    // Skip deprecated calendar variants.
    if (calendar == mozilla::MakeStringSpan("islamic-rgsa")) {
      continue;
    }

    if (StringEqualsAscii(string, calendar.data(), calendar.size())) {
      *result = true;
      return true;
    }
  }

  *result = false;
  return true;
}

/**
 * Return `true` in |result| iff `string` is a supported collation for the
 * requested locale. Otherwise set |result| to `false`.
 */
static bool IsSupportedCollation(JSContext* cx, LanguageId locale,
                                 Handle<JSLinearString*> string, bool* result) {
  StringAsciiChars collation(string);
  if (!collation.init(cx)) {
    return false;
  }

  *result = mozilla::intl::Collator::IsSupportedCollation(locale.toString(),
                                                          collation);
  return true;
}

/**
 * Return `true` iff `string` is a supported collation "case first" value.
 * Otherwise return `false`.
 */
template <typename CharT>
static bool IsSupportedCollationCaseFirst(mozilla::Range<const CharT> string) {
  // [[CaseFirst]] is one of the String values "upper", "lower", or "false".
  static constexpr auto caseFirst = std::to_array<std::string_view>({
      "false",
      "lower",
      "upper",
  });

  return std::any_of(caseFirst.begin(), caseFirst.end(), [&](const auto& a) {
    return a.length() == string.length() &&
           EqualChars(a.data(), string.begin().get(), a.length());
  });
}

static bool IsSupportedCollationCaseFirst(const JSLinearString* string) {
  MOZ_ASSERT(StringIsAscii(string));

  JS::AutoCheckCannotGC nogc;
  if (string->hasLatin1Chars()) {
    return IsSupportedCollationCaseFirst(string->latin1Range(nogc));
  }
  return IsSupportedCollationCaseFirst(string->twoByteRange(nogc));
}

/**
 * Return `true` iff `string` is a supported collation "numeric" value.
 * Otherwise return `false`.
 */
template <typename CharT>
static bool IsSupportedCollationNumeric(mozilla::Range<const CharT> string) {
  // [[Numeric]] is a Boolean value. (We use the string representation here.)
  static constexpr auto numeric = std::to_array<std::string_view>({
      "false",
      "true",
  });

  return std::any_of(numeric.begin(), numeric.end(), [&](const auto& a) {
    return a.length() == string.length() &&
           EqualChars(a.data(), string.begin().get(), a.length());
  });
}

static bool IsSupportedCollationNumeric(const JSLinearString* string) {
  MOZ_ASSERT(StringIsAscii(string));

  JS::AutoCheckCannotGC nogc;
  if (string->hasLatin1Chars()) {
    return IsSupportedCollationNumeric(string->latin1Range(nogc));
  }
  return IsSupportedCollationNumeric(string->twoByteRange(nogc));
}

/**
 * Return `true` iff `string` is a supported hour cycle value. Otherwise return
 * `false`.
 */
template <typename CharT>
static bool IsSupportedHourCycle(mozilla::Range<const CharT> string) {
  // [[LocaleData]].[[<locale>]].[[hc]] must be « null, "h11", "h12", "h23",
  // "h24" ».
  //
  // The `null` case is handled in the caller.
  static constexpr auto hourCycles = std::to_array<std::string_view>({
      "h11",
      "h12",
      "h23",
      "h24",
  });

  return std::any_of(hourCycles.begin(), hourCycles.end(), [&](const auto& a) {
    return a.length() == string.length() &&
           EqualChars(a.data(), string.begin().get(), a.length());
  });
}

static bool IsSupportedHourCycle(const JSLinearString* string) {
  // The hour cycle value can be `null`.
  if (!string) {
    return true;
  }
  MOZ_ASSERT(StringIsAscii(string));

  JS::AutoCheckCannotGC nogc;
  if (string->hasLatin1Chars()) {
    return IsSupportedHourCycle(string->latin1Range(nogc));
  }
  return IsSupportedHourCycle(string->twoByteRange(nogc));
}

/**
 * Return `true` iff `string` is a supported numbering system. Otherwise return
 * `false`.
 */
template <typename CharT>
static bool IsSupportedNumberingSystem(std::basic_string_view<CharT> string) {
  // ICU doesn't have an API to determine the set of numbering systems supported
  // for a locale; it generally pretends that any numbering system can be used
  // with any locale. Supporting a decimal numbering system (where only the
  // digits are replaced) is easy, so we offer them all here. Algorithmic
  // numbering systems are typically tied to one locale, so for lack of
  // information we don't offer them.

  // Sorted list of allowed decimal numbering systems.
  static constexpr auto numberingSystems = std::to_array<std::string_view>(
      {NUMBERING_SYSTEMS_WITH_SIMPLE_DIGIT_MAPPINGS});

  return std::binary_search(numberingSystems.begin(), numberingSystems.end(),
                            string, [](const auto& a, const auto& b) {
                              return CompareChars(a.data(), a.length(),
                                                  b.data(), b.length()) < 0;
                            });
}

static bool IsSupportedNumberingSystem(const JSLinearString* string) {
  MOZ_ASSERT(StringIsAscii(string));

  JS::AutoCheckCannotGC nogc;

  if (string->hasLatin1Chars()) {
    const auto* chars = string->latin1Chars(nogc);
    std::string_view sv{reinterpret_cast<const char*>(chars), string->length()};
    return IsSupportedNumberingSystem(sv);
  }

  const auto* chars = string->twoByteChars(nogc);
  std::u16string_view sv{chars, string->length()};
  return IsSupportedNumberingSystem(sv);
}

/**
 * Return the default locale.
 */
bool js::intl::DefaultLocale(JSContext* cx, LanguageId* result) {
  return cx->global()->globalIntlData().defaultLocale(cx, result);
}

/**
 * Return the default calendar of a locale.
 */
JSLinearString* js::intl::DefaultCalendar(JSContext* cx,
                                          const JSLinearString* locale) {
  auto langId = ToLanguageId(cx, locale);
  MOZ_RELEASE_ASSERT(langId, "locale expected to be a valid data locale");

  auto localeStr = langId->toString();

  auto calendar = mozilla::intl::Calendar::TryCreate(localeStr.c_str());
  if (calendar.isErr()) {
    ReportInternalError(cx, calendar.unwrapErr());
    return nullptr;
  }

  auto type = calendar.unwrap()->GetBcp47Type();
  if (type.isErr()) {
    ReportInternalError(cx, type.unwrapErr());
    return nullptr;
  }

  return NewStringCopy<CanGC>(cx, type.unwrap());
}

/**
 * Return the default numbering system of a locale.
 */
JSLinearString* js::intl::DefaultNumberingSystem(JSContext* cx,
                                                 LanguageId locale) {
  auto localeStr = locale.toString();

  auto numberingSystem =
      mozilla::intl::NumberingSystem::TryCreate(localeStr.c_str());
  if (numberingSystem.isErr()) {
    ReportInternalError(cx, numberingSystem.unwrapErr());
    return nullptr;
  }

  auto name = numberingSystem.inspect()->GetName();
  if (name.isErr()) {
    ReportInternalError(cx, name.unwrapErr());
    return nullptr;
  }

  return NewStringCopy<CanGC>(cx, name.unwrap());
}

/**
 * Return the default numbering system of a locale.
 */
JSLinearString* js::intl::DefaultNumberingSystem(JSContext* cx,
                                                 const JSLinearString* locale) {
  auto langId = ToLanguageId(cx, locale);
  MOZ_RELEASE_ASSERT(langId, "locale expected to be a valid data locale");

  return DefaultNumberingSystem(cx, *langId);
}

/**
 * Check if a locale supports the requested value for a Unicode extension key.
 */
static bool IsSupported(JSContext* cx, LocaleData localeData, LanguageId locale,
                        UnicodeExtensionKey key, Handle<JSLinearString*> value,
                        bool* result) {
  switch (key) {
    case UnicodeExtensionKey::Calendar: {
      return IsSupportedCalendar(cx, locale, value, result);
    }
    case UnicodeExtensionKey::Collation: {
      // Search collations can't use a different collation.
      if (localeData == LocaleData::CollatorSearch) {
        *result = false;
        return true;
      }
      return IsSupportedCollation(cx, locale, value, result);
    }
    case UnicodeExtensionKey::CollationCaseFirst: {
      *result = IsSupportedCollationCaseFirst(value);
      return true;
    }
    case UnicodeExtensionKey::CollationNumeric: {
      *result = IsSupportedCollationNumeric(value);
      return true;
    }
    case UnicodeExtensionKey::FirstDayOfWeek: {
      // Not used as an option.
      break;
    }
    case UnicodeExtensionKey::HourCycle: {
      *result = IsSupportedHourCycle(value);
      return true;
    }
    case UnicodeExtensionKey::NumberingSystem: {
      *result = IsSupportedNumberingSystem(value);
      return true;
    }
  }
  MOZ_CRASH("invalid Unicode extension key");
}

/**
 * ResolveLocale ( availableLocales, requestedLocales, options,
 * relevantExtensionKeys, localeData )
 */
bool js::intl::ResolveLocale(
    JSContext* cx, AvailableLocaleKind availableLocales,
    Handle<ArrayObject*> requestedLocales, Handle<LocaleOptions> options,
    mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys,
    LocaleData localeData, JS::MutableHandle<ResolvedLocale> result) {
  // Steps 1-4.
  //
  // BestFitMatcher not implemented in this implementation.
  Rooted<LookupMatcherResult> match(cx);
  if (!LookupMatcher(cx, availableLocales, requestedLocales, &match)) {
    return false;
  }

  // Step 5.
  auto foundLocale = match.locale();

  // Steps 6-7. (Not applicable in our implementation.)

  // Step 8.
  result.set(ResolvedLocale{});

  // Step 9. (Not applicable in our implementation.)

  // Steps 10-11.
  UnicodeExtensionKeywords keywords{};
  if (CanHaveUnicodeExtensionComponents(match.requestedLocale())) {
    keywords = UnicodeExtensionComponents(match.requestedLocale());
  }

  // Step 12.
  mozilla::EnumSet<UnicodeExtensionKey> supportedKeywords = {};

  // Step 13.
  Rooted<mozilla::Maybe<JSLinearString*>> extensionValue(cx);
  Rooted<JSLinearString*> keywordsValue(cx);
  Rooted<JSLinearString*> optionsValue(cx);
  for (auto key : relevantExtensionKeys) {
    // Steps 13.a-b. (Not applicable in our implementation.)
    extensionValue = mozilla::Nothing();

    // Steps 13.c-d. (Not applicable in our implementation.)

    // Step 13.e.
    bool isSupportedKeyword = false;

    // Step 13.f.
    if (keywords.has(key)) {
      // Step 13.f.i.
      auto [start, length] = keywords.get(key);

      // Step 13.f.ii.
      if (length > 0) {
        MOZ_ASSERT(start + length <= match.requestedLocale()->length());

        keywordsValue =
            NewDependentString(cx, match.requestedLocale(), start, length);
        if (!keywordsValue) {
          return false;
        }
      } else {
        keywordsValue = cx->names().true_;
      }

      // Steps 13.f.iii-iv. (Moved below)
    }

    // Steps 13.g-k.
    //
    // Options override all.
    if (options.hasUnicodeExtension(key)) {
      // Step 13.g. (Not applicable in our implementation.)

      // Step 13.h.
      optionsValue = options.getUnicodeExtension(key);

      // Step 13.i. (Not applicable)

      // Step 13.j.
      //
      // String options are already canonicalized in our implementation.

      // Step 13.j.iii.i.
      //
      // No currently supported options value is an empty string.
      MOZ_ASSERT_IF(optionsValue, !optionsValue->empty());

      bool supported;
      if (!IsSupported(cx, localeData, foundLocale, key, optionsValue,
                       &supported)) {
        return false;
      }

      if (supported) {
        extensionValue = mozilla::Some(optionsValue.get());

        if (optionsValue && keywords.has(key)) {
          MOZ_ASSERT(keywordsValue && !keywordsValue->empty());
          isSupportedKeyword = EqualStrings(keywordsValue, optionsValue);
        }
      }
    }

    // Steps 13.f.iii-iv.
    //
    // Locale tag may override.
    if (extensionValue.isNothing() && keywords.has(key)) {
      MOZ_ASSERT(keywordsValue && !keywordsValue->empty());

      bool supported;
      if (!IsSupported(cx, localeData, foundLocale, key, keywordsValue,
                       &supported)) {
        return false;
      }

      if (supported) {
        extensionValue = mozilla::Some(keywordsValue.get());
        isSupportedKeyword = true;
      }
    }

    // Step 13.l.
    if (isSupportedKeyword) {
      supportedKeywords += key;
    }

    // Step 13.m.
    if (extensionValue.isSome()) {
      result.setUnicodeExtension(key, *extensionValue);
    }
  }

  // Step 14.
  result.setUnicodeKeywords(supportedKeywords);

  // Step 15.
  result.setDataLocale(foundLocale);

  // Step 16.
  return true;
}

static ArrayObject* LocalesListToArray(JSContext* cx,
                                       Handle<LocalesList> locales) {
  auto* array = NewDenseFullyAllocatedArray(cx, locales.length());
  if (!array) {
    return nullptr;
  }
  array->setDenseInitializedLength(locales.length());

  for (size_t i = 0; i < locales.length(); i++) {
    array->initDenseElement(i, StringValue(locales[i]));
  }
  return array;
}

ArrayObject* js::intl::SupportedLocalesOf(JSContext* cx,
                                          AvailableLocaleKind availableLocales,
                                          Handle<Value> locales,
                                          Handle<Value> options) {
  Rooted<LocalesList> requestedLocales(cx, cx);
  if (!CanonicalizeLocaleList(cx, locales, &requestedLocales)) {
    return nullptr;
  }

  Rooted<LocalesList> supportedLocales(cx, cx);
  if (!SupportedLocales(cx, availableLocales, requestedLocales, options,
                        &supportedLocales)) {
    return nullptr;
  }

  return LocalesListToArray(cx, supportedLocales);
}

ArrayObject* js::intl::CanonicalizeLocaleList(JSContext* cx,
                                              Handle<Value> locales) {
  Rooted<LocalesList> requestedLocales(cx, cx);
  if (!CanonicalizeLocaleList(cx, locales, &requestedLocales)) {
    return nullptr;
  }

  return LocalesListToArray(cx, requestedLocales);
}

/**
 * Certain old, commonly-used language tags that lack a script, are expected to
 * nonetheless imply one. This object maps these old-style tags to modern
 * equivalents.
 */
struct OldStyleLanguageTagMapping {
  LanguageId oldStyle;
  LanguageId modernStyle;

  consteval OldStyleLanguageTagMapping(std::string_view oldStyle,
                                       std::string_view modernStyle)
      : oldStyle(LanguageId::fromValidBcp49(oldStyle)),
        modernStyle(LanguageId::fromValidBcp49(modernStyle)) {}
};

static constexpr OldStyleLanguageTagMapping oldStyleLanguageTagMappings[] = {
    {"pa-PK", "pa-Arab-PK"}, {"zh-CN", "zh-Hans-CN"}, {"zh-HK", "zh-Hant-HK"},
    {"zh-SG", "zh-Hans-SG"}, {"zh-TW", "zh-Hant-TW"},
};

static auto AddImplicitScriptToLocale(LanguageId locale) {
  for (const auto& [oldStyle, modernStyle] : oldStyleLanguageTagMappings) {
    if (locale == oldStyle) {
      return modernStyle;
    }
  }
  return locale;
}

bool js::intl::ComputeDefaultLocale(JSContext* cx, LanguageId* result) {
  // Certain old-style language tags lack a script code, but in current usage
  // they *would* include a script code. Map these over to modern forms.
  auto candidate = AddImplicitScriptToLocale(cx->realm()->getLocale());

  // 9.1 Internal slots of Service Constructors
  //
  // - [[AvailableLocales]] is a List [...]. The list must include the value
  //   returned by the DefaultLocale abstract operation (6.2.4), [...].
  //
  // That implies we must ignore any candidate which isn't supported by all
  // Intl service constructors.

  mozilla::Maybe<LanguageId> supportedCollator{};
  if (!BestAvailableLocale(cx, AvailableLocaleKind::Collator, candidate,
                           &supportedCollator)) {
    return false;
  }

  mozilla::Maybe<LanguageId> supportedDateTimeFormat{};
  if (!BestAvailableLocale(cx, AvailableLocaleKind::DateTimeFormat, candidate,
                           &supportedDateTimeFormat)) {
    return false;
  }

#ifdef DEBUG
  // Note: We don't test the supported locales of the remaining Intl service
  // constructors, because the set of supported locales is exactly equal to
  // the set of supported locales of Intl.DateTimeFormat.
  for (auto kind : {
           AvailableLocaleKind::DisplayNames,
           AvailableLocaleKind::DurationFormat,
           AvailableLocaleKind::ListFormat,
           AvailableLocaleKind::NumberFormat,
           AvailableLocaleKind::PluralRules,
           AvailableLocaleKind::RelativeTimeFormat,
           AvailableLocaleKind::Segmenter,
       }) {
    mozilla::Maybe<LanguageId> supported{};
    if (!BestAvailableLocale(cx, kind, candidate, &supported)) {
      return false;
    }
    MOZ_ASSERT(supported == supportedDateTimeFormat);
  }
#endif

  // Accept the candidate locale if it is supported by all Intl service
  // constructors.
  if (supportedCollator && supportedDateTimeFormat) {
    // Use the actually supported locale instead of the candidate locale. For
    // example when the candidate locale "en-US-posix" is supported through
    // "en-US", use "en-US" as the default locale.
    //
    // Also prefer the supported locale with more subtags. For example when
    // requesting "de-CH" and Intl.DateTimeFormat supports "de-CH", but
    // Intl.Collator only "de", still return "de-CH" as the result.
    if (supportedCollator->isPrefixOf(*supportedDateTimeFormat)) {
      *result = *supportedDateTimeFormat;
    } else {
      *result = *supportedCollator;
    }
  } else {
    // Return the last ditch locale if the candidate locale isn't supported.
    *result = LastDitchLocale();
  }
  return true;
}
